#include <stdafx.h>

#include "ScriptContext.h"

#include <CET.h>

// TODO: proper exception handling for Lua funcs!
template <typename ...Args>
static sol::protected_function_result TryLuaFunction(std::shared_ptr<spdlog::logger> aLogger, sol::function aFunc, Args... aArgs)
{
    sol::protected_function_result result{ };
    if (aFunc)
    {
        try
        {
            result = aFunc(aArgs...);
        }
        catch(std::exception& e)
        {
            aLogger->error(e.what());
        }
        if (!result.valid())
        {
            const sol::error cError = result;
            aLogger->error(cError.what());
        }
    }
    return result;
}

ScriptContext::ScriptContext(LuaSandbox& aLuaSandbox, const std::filesystem::path& acPath, const std::string& acName)
    : m_sandbox(aLuaSandbox)
    , m_sandboxID(aLuaSandbox.CreateSandbox(acPath, acName))
    , m_name(acName)
{
    auto state = aLuaSandbox.GetState();

    auto& sb = m_sandbox[m_sandboxID];
    auto& env = sb.GetEnvironment();
    m_logger = env["__logger"].get<std::shared_ptr<spdlog::logger>>();

    env["registerForEvent"] = [this](const std::string& acName, sol::function aCallback)
    {
        if(acName == "onInit")
            m_onInit = aCallback;
        else if(acName == "onTweak")
            m_onTweak = aCallback;
        else if(acName == "onShutdown")
            m_onShutdown = aCallback;
        else if(acName == "onUpdate")
            m_onUpdate = aCallback;
        else if(acName == "onDraw")
            m_onDraw = aCallback;
        else if(acName == "onOverlayOpen")
            m_onOverlayOpen = aCallback;
        else if(acName == "onOverlayClose")
            m_onOverlayClose = aCallback;
        else
            m_logger->error("Tried to register an unknown event '{}'!", acName);
    };

    env["registerHotkey"] = [this, &aLuaSandbox](const std::string& acID, const std::string& acDescription, sol::function aCallback)
    {
        if (acID.empty() ||
            (std::ranges::find_if(acID, [](char c){ return !(isalpha(c) || isdigit(c) || c == '_'); }) != acID.cend()))
        {
            m_logger->error("Tried to register a hotkey with an incorrect ID format '{}'! ID needs to be alphanumeric without any whitespace or special characters (exception being '_' which is allowed in ID)!", acID);
            return;
        }

        if (acDescription.empty())
        {
            m_logger->error("Tried to register a hotkey with an empty description! (ID of hotkey handler: {})", acID);
            return;
        }

        auto loggerRef = m_logger;
        std::string vkBindID = m_name + '.' + acID;
        VKBind vkBind = {vkBindID, acDescription, [&aLuaSandbox, loggerRef, aCallback]()
        {
            auto state = aLuaSandbox.GetState();
            TryLuaFunction(loggerRef, aCallback);
        }};

        m_vkBindInfos.emplace_back(VKBindInfo{vkBind});
    };
    
    env["registerInput"] = [this, &aLuaSandbox](const std::string& acID, const std::string& acDescription, sol::function aCallback) {
        if (acID.empty() ||
            (std::ranges::find_if(acID, [](char c) { return !(isalpha(c) || isdigit(c) || c == '_'); }) != acID.cend()))
        {
            m_logger->error(
                "Tried to register input with an incorrect ID format '{}'! ID needs to be alphanumeric without any "
                "whitespace or special characters (exception being '_' which is allowed in ID)!",
                acID);
            return;
        }

        if (acDescription.empty())
        {
            m_logger->error("Tried to register an input with an empty description! (ID of input handler: {})", acID);
            return;
        }

        auto loggerRef = m_logger;
        std::string vkBindID = m_name + '.' + acID;
        VKBind vkBind = {vkBindID, acDescription, [&aLuaSandbox, loggerRef, aCallback](bool isDown)
        {
            auto state = aLuaSandbox.GetState();
            TryLuaFunction(loggerRef, aCallback, isDown);
        }};

        m_vkBindInfos.emplace_back(VKBindInfo{vkBind});
    };

    // TODO: proper exception handling!
    try
    {
        const auto path = acPath / "init.lua";
        const auto result = sb.ExecuteFile(path.string());

        if (result.valid())
        {
            m_initialized = true;
            m_object = result;
        }
        else
        {
            const sol::error cError = result;
            m_logger->error(cError.what());
        }
    }
    catch(std::exception& e)
    {
        m_logger->error(e.what());
    }
}

ScriptContext::ScriptContext(ScriptContext&& other) noexcept : ScriptContext(other)
{
    other.m_initialized = false;
}

ScriptContext::~ScriptContext()
{
    if (m_initialized)
        TriggerOnShutdown();

    m_logger->flush();
}

bool ScriptContext::IsValid() const
{
    return m_initialized;
}

const TiltedPhoques::Vector<VKBindInfo>& ScriptContext::GetBinds() const
{
    return m_vkBindInfos;
}

void ScriptContext::TriggerOnTweak() const
{
    auto state = m_sandbox.GetState();

    TryLuaFunction(m_logger, m_onTweak);
}

void ScriptContext::TriggerOnInit() const
{
    auto state = m_sandbox.GetState();

    TryLuaFunction(m_logger, m_onInit);
}

void ScriptContext::TriggerOnUpdate(float aDeltaTime) const
{
    auto state = m_sandbox.GetState();

    TryLuaFunction(m_logger, m_onUpdate, aDeltaTime);
}

void ScriptContext::TriggerOnDraw() const
{
    auto state = m_sandbox.GetState();

    TryLuaFunction(m_logger, m_onDraw);
}
    
void ScriptContext::TriggerOnOverlayOpen() const
{
    auto state = m_sandbox.GetState();

    TryLuaFunction(m_logger, m_onOverlayOpen);
}

void ScriptContext::TriggerOnOverlayClose() const
{
    auto state = m_sandbox.GetState();

    TryLuaFunction(m_logger, m_onOverlayClose);
}

sol::object ScriptContext::GetRootObject() const
{
    return m_object;
}

void ScriptContext::TriggerOnShutdown() const
{
    auto state = m_sandbox.GetState();

    TryLuaFunction(m_logger, m_onShutdown);
}
