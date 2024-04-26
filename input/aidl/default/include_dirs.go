package tvinput

import (
    "android/soong/android"
    "android/soong/cc"
    "fmt"
    "strings"
)

func init() {
    fmt.Println("tvinput want to conditional Compile")
    android.RegisterModuleType("cc_tvinput", pqInitFactory)
}

func pqInitFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, pqInitDefaults)
    return module
}

func pqInitDefaults(ctx android.LoadHookContext) {
    fmt.Println("tvinput_defaults pqInitDefaults")
    type props struct {
        Include_dirs []string
    }
    p := &props{}
    p.Include_dirs = getIncludeDirs(ctx)
    ctx.AppendProperties(p)
}

func getIncludeDirs(ctx android.BaseContext) ([]string) {
    var dirs []string
    var soc string = getTargetSoc(ctx)
    var path string = "hardware/rockchip/libvisionpq/lib/Android/" + soc + "/include"
    if android.ExistentPathForSource(ctx, path).Valid() == true {
        dirs = append(dirs, path)
    }
    return dirs
}

func getTargetSoc(ctx android.BaseContext) (string) {
    var target_board_platform string = strings.ToLower(ctx.AConfig().Getenv("TARGET_BOARD_PLATFORM"))
    return target_board_platform
}
