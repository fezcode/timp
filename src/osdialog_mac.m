// osdialog_mac.m — macOS backend for osdialog.h (Cocoa).
// NSOpenPanel for the file picker, NSWorkspace for reveal, layer corner
// rounding on the borderless NSWindow. Compile with ARC (-fobjc-arc).
#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "osdialog.h"

int os_open_audio_files(void (*on_file)(const char *, void *), void *ud) {
    @autoreleasepool {
        NSOpenPanel *panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = YES;
        panel.title = @"Open Audio Files";
        NSMutableArray<UTType *> *types = [NSMutableArray array];
        for (NSString *ext in @[ @"mp3", @"flac", @"ogg", @"wav", @"m4a", @"opus" ]) {
            UTType *t = [UTType typeWithFilenameExtension:ext];
            if (t) [types addObject:t];
        }
        if (types.count) panel.allowedContentTypes = types;

        int count = 0;
        if ([panel runModal] == NSModalResponseOK) {
            for (NSURL *url in panel.URLs) {
                const char *p = url.fileSystemRepresentation;
                if (p) { on_file(p, ud); count++; }
            }
        }
        return count;
    }
}

// hwnd is raylib's GetWindowHandle() → the NSWindow of the GLFW window.
// The window is borderless (FLAG_WINDOW_UNDECORATED); clip the content view's
// layer instead of DWM regions. Radius arrives in logical px == points here.
void os_round_window(void *hwnd, int w, int h, int radius) {
    (void)w; (void)h;
    NSWindow *win = (__bridge NSWindow *)hwnd;
    if (!win) return;
    win.opaque = NO;
    win.backgroundColor = [NSColor clearColor];
    win.hasShadow = YES;
    NSView *v = win.contentView;
    v.wantsLayer = YES;
    v.layer.cornerRadius = (CGFloat)radius;
    v.layer.masksToBounds = YES;
    [win invalidateShadow];
}

// argv is already UTF-8 on macOS.
char **os_args_utf8(int argc, char **argv, int *out_count) {
    *out_count = argc;
    return argv;
}

void os_focus_window(void *hwnd) {
    NSWindow *win = (__bridge NSWindow *)hwnd;
    [NSApp activateIgnoringOtherApps:YES];
    if (win) {
        if (win.miniaturized) [win deminiaturize:nil];
        [win makeKeyAndOrderFront:nil];
    }
}

void os_reveal_dir(const char *utf8_path) {
    if (!utf8_path || !utf8_path[0]) return;
    @autoreleasepool {
        NSString *s = [NSString stringWithUTF8String:utf8_path];
        if (s) [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:s isDirectory:YES]];
    }
}

// ---- Finder "Open With" / double-click ----
// macOS delivers opened documents to the app delegate (AppKit converts the
// kAEOpenDocuments Apple Event into -application:openFiles: during its own
// event routing — a raw NSAppleEventManager handler gets overwritten when
// NSApplication finishes launching). GLFW's delegate doesn't implement the
// method, so graft it on with the runtime BEFORE InitWindow registers the
// delegate. Received paths go through our own single-instance socket: the
// listen thread queues them and the main loop plays them like a CLI handoff.
#include "singleinst.h"
#import <objc/runtime.h>

static void open_files_imp(id self, SEL _cmd, NSApplication *app, NSArray<NSString *> *files) {
    (void)self; (void)_cmd;
    for (NSString *f in files) singleinst_send_file(f.fileSystemRepresentation);
    [app replyToOpenOrPrint:NSApplicationDelegateReplySuccess];
}

void os_open_files_handler_install(void) {
    Class cls = NSClassFromString(@"GLFWApplicationDelegate");
    if (cls) class_addMethod(cls, @selector(application:openFiles:), (IMP)open_files_imp, "v@:@@");
}

#endif // __APPLE__
