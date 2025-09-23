const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const mode = b.standardOptimizeOption(.{});
    const module = b.addModule("main", .{
        .target = target,
        .optimize = mode,
        .link_libc = true,
    });

    module.addCSourceFile(.{
        .file = b.path("main.c"),
        .flags = &[_][]const u8{ "-Wall", "-Wextra" },
    });

    const exe = b.addExecutable(.{
        .name = "narnia",
        .root_module = module,
    });

    exe.linkSystemLibrary("curl");
    exe.linkSystemLibrary("clipboard");
    exe.linkSystemLibrary("ncurses");

    b.installArtifact(exe);
}
