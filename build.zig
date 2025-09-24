const std = @import("std");

pub fn build(b: *std.Build) void {
    // Standard target options allows the person running `zig build` to choose
    // what target to build for. Here we do not override the defaults, which
    // means any target is allowed, and the default is native. Other options
    // for restricting supported target set are available.
    const target = b.standardTargetOptions(.{});

    // Standard release options allow the person running `zig build` to select
    // between Debug, ReleaseSafe, ReleaseFast, and ReleaseSmall.
    const mode = b.standardOptimizeOption(.{});
    const module = b.addModule("main", .{
        .target = target,
        .optimize = mode,
        .link_libc = true,
        .strip = true,
    });

    module.addIncludePath(b.path("include"));

    module.addCSourceFile(.{
        .file = b.path("main.c"),
        .flags = &[_][]const u8{ "-Wall", "-Wextra" },
    });

    module.addCSourceFile(.{
        .file = b.path("include/clipboard.c"),
        .flags = &[_][]const u8{ "-Wall", "-Wextra" },
    });

    const exe = b.addExecutable(.{
        .name = "narnia",
        .root_module = module,
    });

    exe.linkSystemLibrary("curl");
    exe.linkSystemLibrary("ncurses");

    const pkg_config_result = std.process.Child.run(.{
        .allocator = b.allocator,
        .argv = &[_][]const u8{ "pkg-config", "--exists", "x11" },
    }) catch null;

    if (pkg_config_result != null and pkg_config_result.?.term.Exited == 0) {
        module.addCMacro("HAVE_X11", "1");
        exe.linkSystemLibrary("X11");
    }

    const wayland_result = std.process.Child.run(.{
        .allocator = b.allocator,
        .argv = &[_][]const u8{ "pkg-config", "--exists", "wayland-client" },
    }) catch null;

    if (wayland_result != null and wayland_result.?.term.Exited == 0) {
        module.addCMacro("HAVE_WAYLAND", "1");
        exe.linkSystemLibrary("wayland-client");
    }

    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());

    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    const run_step = b.step("run", "Run the app");
    run_step.dependOn(&run_cmd.step);
}
