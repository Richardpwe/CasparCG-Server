/*
 * Copyright (c) 2026 CasparCG contributors
 *
 * This file is part of CasparCG (www.casparcg.com).
 *
 * CasparCG is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "web.h"

#include <common/env.h>
#include <common/executor.h>
#include <common/except.h>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/range/algorithm/remove_if.hpp>

#include <include/cef_app.h>
#include <include/cef_version.h>

#ifdef WIN32
#include <accelerator/d3d/d3d_device.h>
#endif

#include <memory>
#include <utility>
#include <vector>

namespace caspar::web {

namespace {

std::unique_ptr<executor> g_cef_executor;

void caspar_log(const CefRefPtr<CefBrowser>&        browser,
                boost::log::trivial::severity_level level,
                const std::string&                  message)
{
    if (browser == nullptr) {
        return;
    }

    auto msg = CefProcessMessage::Create(LOG_MESSAGE_NAME);
    msg->GetArgumentList()->SetInt(0, level);
    msg->GetArgumentList()->SetString(1, message);

    CefRefPtr<CefFrame> main_frame = browser->GetMainFrame();
    if (main_frame) {
        main_frame->SendProcessMessage(PID_BROWSER, msg);
    }
}

class remove_handler : public CefV8Handler
{
    CefRefPtr<CefBrowser> browser_;

  public:
    explicit remove_handler(const CefRefPtr<CefBrowser>& browser)
        : browser_(browser)
    {
    }

    bool Execute(const CefString&,
                 CefRefPtr<CefV8Value>,
                 const CefV8ValueList&,
                 CefRefPtr<CefV8Value>&,
                 CefString&) override
    {
        if (!CefCurrentlyOn(TID_RENDERER)) {
            return false;
        }

        CefRefPtr<CefFrame> main_frame = browser_->GetMainFrame();
        if (main_frame) {
            main_frame->SendProcessMessage(PID_BROWSER, CefProcessMessage::Create(REMOVE_MESSAGE_NAME));
        }

        return true;
    }

    IMPLEMENT_REFCOUNTING(remove_handler);
};

class post_message_handler : public CefV8Handler
{
    CefRefPtr<CefBrowser> browser_;

  public:
    explicit post_message_handler(const CefRefPtr<CefBrowser>& browser)
        : browser_(browser)
    {
    }

    bool Execute(const CefString&,
                 CefRefPtr<CefV8Value>,
                 const CefV8ValueList& arguments,
                 CefRefPtr<CefV8Value>&,
                 CefString& exception) override
    {
        if (!CefCurrentlyOn(TID_RENDERER) || arguments.size() != 1 || !arguments[0]->IsString()) {
            exception = "postMessage expects one string argument";
            return true;
        }

        auto message = CefProcessMessage::Create(WEB_MESSAGE_NAME);
        message->GetArgumentList()->SetString(0, arguments[0]->GetStringValue());

        CefRefPtr<CefFrame> main_frame = browser_->GetMainFrame();
        if (main_frame) {
            main_frame->SendProcessMessage(PID_BROWSER, message);
        }
        return true;
    }

    IMPLEMENT_REFCOUNTING(post_message_handler);
};

class renderer_application
    : public CefApp
    , public CefRenderProcessHandler
{
    std::vector<CefRefPtr<CefV8Context>> contexts_;
    const bool                           enable_gpu_;
    const bool                           shared_texture_;

  public:
    explicit renderer_application(const bool enable_gpu, const bool shared_texture)
        : enable_gpu_(enable_gpu)
        , shared_texture_(shared_texture)
    {
    }

    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }

    void
    OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) override
    {
        if (!frame->IsMain()) {
            return;
        }

        caspar_log(
            browser, boost::log::trivial::trace, "context for frame " + frame->GetIdentifier().ToString() + " created");
        contexts_.push_back(context);

        auto window = context->GetGlobal();
        window->SetValue(
            "remove", CefV8Value::CreateFunction("remove", new remove_handler(browser)), V8_PROPERTY_ATTRIBUTE_NONE);

        auto native_bridge = CefV8Value::CreateObject(nullptr, nullptr);
        native_bridge->SetValue("postMessage",
                                CefV8Value::CreateFunction("postMessage", new post_message_handler(browser)),
                                V8_PROPERTY_ATTRIBUTE_READONLY);
        window->SetValue("casparNative", native_bridge, V8_PROPERTY_ATTRIBUTE_READONLY);

        CefRefPtr<CefV8Value>     ret;
        CefRefPtr<CefV8Exception> exception;
        const bool                injected = context->Eval(R"(
            window.caspar = window.casparcg = {};
        )",
                                            CefString(),
                                            1,
                                            ret,
                                            exception);

        if (!injected) {
            caspar_log(browser, boost::log::trivial::error, "Could not inject javascript animation code.");
        }
    }

    void OnContextReleased(CefRefPtr<CefBrowser>   browser,
                           CefRefPtr<CefFrame>     frame,
                           CefRefPtr<CefV8Context> context) override
    {
        if (!frame->IsMain()) {
            return;
        }

        const auto removed =
            boost::remove_if(contexts_, [&](const CefRefPtr<CefV8Context>& item) { return item->IsSame(context); });

        if (removed != contexts_.end()) {
            caspar_log(browser,
                       boost::log::trivial::trace,
                       "context for frame " + frame->GetIdentifier().ToString() + " released");
        } else {
            caspar_log(browser,
                       boost::log::trivial::warning,
                       "context for frame " + frame->GetIdentifier().ToString() + " released, but not found");
        }
    }

    void OnBrowserDestroyed(CefRefPtr<CefBrowser>) override { contexts_.clear(); }

    void OnBeforeCommandLineProcessing(const CefString& process_type, CefRefPtr<CefCommandLine> command_line) override
    {
        if (enable_gpu_) {
            command_line->AppendSwitch("enable-webgl");

            auto default_backend = L"";
#if __unix__
            if (getenv("DISPLAY") == nullptr) {
                default_backend = L"vulkan";
            }
#endif

            const auto backend = env::properties().get(L"configuration.html.angle-backend", default_backend);
            if (!backend.empty()) {
                command_line->AppendSwitchWithValue("use-angle", backend);
            }
        }

#if __unix__
        if (getenv("DISPLAY") == nullptr) {
            command_line->AppendSwitchWithValue("ozone-platform", "headless");
        }
#endif

        command_line->AppendSwitch("disable-web-security");
        command_line->AppendSwitch("enable-begin-frame-scheduling");
        command_line->AppendSwitch("disable-renderer-backgrounding");
        command_line->AppendSwitch("disable-backgrounding-occluded-windows");
        command_line->AppendSwitch("disable-background-timer-throttling");
        command_line->AppendSwitch("enable-media-stream");
        command_line->AppendSwitch("use-fake-ui-for-media-stream");
        command_line->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
        command_line->AppendSwitchWithValue("remote-allow-origins", "*");

        if (process_type.empty() && !enable_gpu_) {
            command_line->AppendSwitch("disable-gpu");
            command_line->AppendSwitch("disable-gpu-compositing");
            command_line->AppendSwitchWithValue("disable-gpu-vsync", "gpu");
        }
    }

    IMPLEMENT_REFCOUNTING(renderer_application);
};

class cef_task : public CefTask
{
    std::promise<void>    promise_;
    std::function<void()> function_;

  public:
    explicit cef_task(std::function<void()> function)
        : function_(std::move(function))
    {
    }

    void Execute() override
    {
        CASPAR_LOG(trace) << "[cef_task] executing task";

        try {
            function_();
            promise_.set_value();
            CASPAR_LOG(trace) << "[cef_task] task succeeded";
        } catch (...) {
            promise_.set_exception(std::current_exception());
            CASPAR_LOG(warning) << "[cef_task] task failed";
        }
    }

    std::future<void> future() { return promise_.get_future(); }

    IMPLEMENT_REFCOUNTING(cef_task);
};

} // namespace

bool intercept_command_line(int argc, char** argv)
{
#ifdef _WIN32
    CefMainArgs main_args;
#else
    CefMainArgs main_args(argc, argv);
#endif

    return CefExecuteProcess(main_args, CefRefPtr<CefApp>(new renderer_application(false, false)), nullptr) >= 0;
}

void init(const core::module_dependencies&)
{
    CefMainArgs main_args;
    g_cef_executor = std::make_unique<executor>(L"cef");
    const bool result = g_cef_executor->invoke([&] {
#ifdef WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
        const auto gpu = is_gpu_shared_texture_enabled();

        CefSettings settings;
        settings.command_line_args_disabled   = false;
        settings.no_sandbox                   = true;
        settings.remote_debugging_port        = env::properties().get(L"configuration.html.remote-debugging-port", 0);
        settings.windowless_rendering_enabled = true;

        auto cache_path = env::properties().get(L"configuration.html.cache-path", L"cef-cache");
        if (!cache_path.empty()) {
            if (!boost::filesystem::path(cache_path).is_absolute()) {
                cache_path = caspar::env::initial_folder() + L"/" + cache_path;
            }
            CASPAR_LOG(info) << L"[web] Using CEF cache path: " << cache_path;
            CefString(&settings.cache_path).FromWString(cache_path);
        }

        return CefInitialize(
            main_args, settings, CefRefPtr<CefApp>(new renderer_application(gpu.first, gpu.second)), nullptr);
    });

    if (!result) {
        CASPAR_LOG(error) << "[web] Failed to initialize CEF";
        g_cef_executor.reset();
        return;
    }

    g_cef_executor->begin_invoke([&] { CefRunMessageLoop(); });
}

void uninit()
{
    if (!g_cef_executor) {
        return;
    }

    invoke([] { CefQuitMessageLoop(); });
    g_cef_executor->begin_invoke([&] { CefShutdown(); });
    g_cef_executor.reset();
}

void invoke(const std::function<void()>& func) { begin_invoke(func).get(); }

std::future<void> begin_invoke(const std::function<void()>& func)
{
    CefRefPtr<cef_task> task = new cef_task(func);

    if (CefCurrentlyOn(TID_UI)) {
        task->Execute();
        return task->future();
    }

    if (CefPostTask(TID_UI, task.get())) {
        return task->future();
    }

    CASPAR_THROW_EXCEPTION(caspar_exception() << msg_info("[cef_executor] Could not post task"));
}

std::pair<bool, bool> is_gpu_shared_texture_enabled()
{
    const bool enable_gpu            = env::properties().get(L"configuration.html.enable-gpu", false);
    bool       shared_texture_enable = false;

#ifdef WIN32
    if (enable_gpu) {
        auto dev = accelerator::d3d::d3d_device::get_device();
        if (!dev) {
            CASPAR_LOG(warning) << L"Failed to create directX device for cef gpu acceleration";
        } else {
            shared_texture_enable = true;
        }
    }
#else
    // Shared-texture rendering is not currently supported on Linux.
#endif

    return std::make_pair(enable_gpu, shared_texture_enable);
}

std::string browser_engine_version() { return std::to_string(CEF_VERSION_MAJOR); }

} // namespace caspar::web
