#include "hooks/PopupHooks.h"
#include "hooks/HookMacros.h"
#include "MewgenicsTypes.h"
#include "Scanner.h"
#include <string>
#include <windows.h>

using ShowYesNoPromptHigh_t = void(__fastcall *)(
    MsvcReleaseModeXString *prompt,
    MsvcReleaseModeStdFunction *yesCb,
    MsvcReleaseModeStdFunction *noCb,
    void **sceneManagerOverride);

using ShowOkPromptHigh_t = void(__fastcall *)(
    MsvcReleaseModeXString *prompt,
    MsvcReleaseModeStdFunction *okCb);

static ShowYesNoPromptHigh_t g_fnShowYesNoPromptHigh = nullptr;
static ShowOkPromptHigh_t    g_fnShowOkPromptHigh    = nullptr;
static thread_local bool     g_popupWasCreated       = false;

HOOK_DEFINE(YesNoPromptInit, void, void*, void*, MsvcReleaseModeXString*, MsvcReleaseModeStdFunction*, MsvcReleaseModeStdFunction*, bool)

static OurClosureInline *__fastcall OurClosure_Clone(OurClosureInline *self, OurClosureInline *out);
static OurClosureInline *__fastcall OurClosure_Move(OurClosureInline *self, OurClosureInline *out);
static void              __fastcall OurClosure_Call(OurClosureInline *self);
static void              __fastcall OurClosure_Destroy(OurClosureInline *self, bool deallocate);

static const void * const g_ourClosureVtable[8] = {
    (void *)OurClosure_Clone,
    (void *)OurClosure_Move,
    (void *)OurClosure_Call,
    nullptr,
    (void *)OurClosure_Destroy,
    (void *)OurClosure_Destroy,
    nullptr,
    nullptr,
};

static OurClosureInline *__fastcall OurClosure_Clone(OurClosureInline *self, OurClosureInline *out) {
    if (!out) return nullptr;
    out->vtable = const_cast<void *>(reinterpret_cast<const void *>(g_ourClosureVtable));
    out->fn_ptr = (self && self->fn_ptr) ? new std::function<void()>(*self->fn_ptr) : nullptr;
    memset(out->pad, 0, sizeof(out->pad));
    out->self_ptr = out;
    return out;
}

static OurClosureInline *__fastcall OurClosure_Move(OurClosureInline *self, OurClosureInline *out) {
    if (!out) return nullptr;
    out->vtable = const_cast<void *>(reinterpret_cast<const void *>(g_ourClosureVtable));
    out->fn_ptr = self ? self->fn_ptr : nullptr;
    if (self) self->fn_ptr = nullptr;
    memset(out->pad, 0, sizeof(out->pad));
    out->self_ptr = out;
    return out;
}

static void __fastcall OurClosure_Call(OurClosureInline *self) {
    if (self && self->fn_ptr && *self->fn_ptr) {
        (*self->fn_ptr)();
    }
}

static void __fastcall OurClosure_Destroy(OurClosureInline *self, bool deallocate) {
    if (!self) return;
    if (self->fn_ptr) {
        delete self->fn_ptr;
        self->fn_ptr = nullptr;
    }
    if (deallocate && self != self->self_ptr) {
        HeapFree(GetProcessHeap(), 0, self);
    }
}

static bool MakeGameFunction(MsvcReleaseModeStdFunction &out, std::function<void()> fn) {
    memset(&out, 0, sizeof(out));
    auto *cl = reinterpret_cast<OurClosureInline *>(&out);
    cl->vtable = const_cast<void *>(reinterpret_cast<const void *>(g_ourClosureVtable));
    cl->fn_ptr = new std::function<void()>(std::move(fn));
    cl->self_ptr = cl;
    return true;
}

static void StringInit(MsvcReleaseModeXString &str, const char *src) {
    const size_t len = src ? strlen(src) : 0;
    str.Mysize = len;
    if (len < 16) {
        str.Myres = 15;
        if (src) strncpy_s(str.Bx.Buf, 16, src, len);
        else     str.Bx.Buf[0] = '\0';
    } else {
        str.Myres = len;
        str.Bx.Ptr = static_cast<char *>(malloc(len + 1));
        if (str.Bx.Ptr) strncpy_s(str.Bx.Ptr, len + 1, src, len);
    }
}

static void __fastcall Hook_YesNoPromptInit(
    void *self,
    void *scene,
    MsvcReleaseModeXString *prompt,
    MsvcReleaseModeStdFunction *yesCb,
    MsvcReleaseModeStdFunction *noCb,
    bool okOnly)
{
    g_popupWasCreated = true;

    ParaboxAPI::PopupShowEvent ev = {};
    ev.self = self;
    ev.prompt = prompt ? prompt->begin() : nullptr;
    ev.okOnly = okOnly;
    ParaboxAPI::OnPopupShow.Publish(ev);

    if (ev.cancelled) return;

    if (g_origYesNoPromptInit) {
        g_origYesNoPromptInit(self, scene, prompt, yesCb, noCb, okOnly);
    }
}

void PopupHooks_Init(MewjectorAPI *mj, uintptr_t gameBase) {
    HOOK_INSTALL(mj, gameBase, YesNoPromptInit,
        "4C 89 4C 24 20 4C 89 44 24 18 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 78 FF FF FF", 16);

    SCAN_SET(mj, gameBase, ShowYesNoPromptHigh,
        "48 8B C4 4C 89 40 18 48 89 50 10 48 89 48 08 53 55 56 57 48 83 EC 68",
        g_fnShowYesNoPromptHigh);

    SCAN_SET(mj, gameBase, ShowOkPromptHigh,
        "4C 8B DC 4D 89 43 18 49 89 53 10 49 89 4B 08 53 56 57 48 83 EC 60",
        g_fnShowOkPromptHigh);
}

namespace ParaboxAPI {

PARABOX_API Event<PopupShowEvent>   OnPopupShow;
PARABOX_API Event<PopupChoiceEvent> OnPopupChoice;

PARABOX_API bool ShowYesNoPopup(
    const char *prompt,
    std::function<void()> onYes,
    std::function<void()> onNo)
{
    if (!g_fnShowYesNoPromptHigh) {
        Log("[Popup] ShowYesNoPromptHigh not found.");
        return false;
    }

    auto wrapYes = [cb = std::move(onYes)]() mutable {
        PopupChoiceEvent cev = {};
        cev.choice = true;
        OnPopupChoice.Publish(cev);
        if (cb) cb();
    };
    auto wrapNo = [cb = std::move(onNo)]() mutable {
        PopupChoiceEvent cev = {};
        cev.choice = false;
        OnPopupChoice.Publish(cev);
        if (cb) cb();
    };

    MsvcReleaseModeXString str = {};
    StringInit(str, prompt);

    MsvcReleaseModeStdFunction yesFn = {}, noFn = {};
    MakeGameFunction(yesFn, std::move(wrapYes));
    MakeGameFunction(noFn, std::move(wrapNo));

    g_popupWasCreated = false;
    bool callSuccess = false;
    __try {
        g_fnShowYesNoPromptHigh(&str, &yesFn, &noFn, nullptr);
        callSuccess = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[Popup] ShowYesNoPromptHigh caught SEH exception.");
        callSuccess = false;
    }

    const bool opened = callSuccess && g_popupWasCreated;
    g_popupWasCreated = false;
    if (!opened) {
        Log("[Popup] ShowYesNoPopup: popup rejected by scene.");
    }
    return opened;
}

PARABOX_API bool ShowYesNoPopup(
    const wchar_t *prompt,
    std::function<void()> onYes,
    std::function<void()> onNo)
{
    if (!prompt) return ShowYesNoPopup("", std::move(onYes), std::move(onNo));
    const int req = WideCharToMultiByte(CP_UTF8, 0, prompt, -1, nullptr, 0, nullptr, nullptr);
    if (req <= 0) return ShowYesNoPopup("", std::move(onYes), std::move(onNo));

    std::string utf8Str(req, '\0');
    WideCharToMultiByte(CP_UTF8, 0, prompt, -1, &utf8Str[0], req, nullptr, nullptr);
    return ShowYesNoPopup(utf8Str.c_str(), std::move(onYes), std::move(onNo));
}

PARABOX_API bool ShowOkPopup(
    const char *prompt,
    std::function<void()> onOk)
{
    if (!g_fnShowOkPromptHigh) {
        Log("[Popup] ShowOkPromptHigh not found.");
        return false;
    }

    auto wrapOk = [cb = std::move(onOk)]() mutable {
        PopupChoiceEvent cev = {};
        cev.choice = true;
        OnPopupChoice.Publish(cev);
        if (cb) cb();
    };

    MsvcReleaseModeXString str = {};
    StringInit(str, prompt);

    MsvcReleaseModeStdFunction okFn = {};
    MakeGameFunction(okFn, std::move(wrapOk));

    g_popupWasCreated = false;
    bool okCallSuccess = false;
    __try {
        g_fnShowOkPromptHigh(&str, &okFn);
        okCallSuccess = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[Popup] ShowOkPromptHigh caught SEH exception.");
        okCallSuccess = false;
    }

    const bool opened = okCallSuccess && g_popupWasCreated;
    g_popupWasCreated = false;
    if (!opened) {
        Log("[Popup] ShowOkPopup: popup rejected by scene.");
    }
    return opened;
}

PARABOX_API bool ShowOkPopup(
    const wchar_t *prompt,
    std::function<void()> onOk)
{
    if (!prompt) return ShowOkPopup("", std::move(onOk));
    const int req = WideCharToMultiByte(CP_UTF8, 0, prompt, -1, nullptr, 0, nullptr, nullptr);
    if (req <= 0) return ShowOkPopup("", std::move(onOk));

    std::string utf8Str(req, '\0');
    WideCharToMultiByte(CP_UTF8, 0, prompt, -1, &utf8Str[0], req, nullptr, nullptr);
    return ShowOkPopup(utf8Str.c_str(), std::move(onOk));
}

} // namespace ParaboxAPI
