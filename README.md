# MetalFX
MetalFx For Unreal (Work in progress)

**This Plugin is Not Final Version. **

> This plugin targets Unreal Engine 5.4 or later and is currently developed against Unreal Engine 5.7.4. Older engine versions are may not supported.



## Guide & Infomation

MetalFX Plugin is **"ENGINE Plugin"**. So This Plugin must be set in Engine/Plugin

* If Not have "MetalCPP" In Your Engine, Please Add that.
  - https://developer.apple.com/metal/cpp/
  - Since **Unreal Engine 5.4**, Metal-cpp is included in most engine code paths.
 
 
* (In Many cases) you need update some engine source code. please run "EngineEditScript.sh" in Source/Thirdparty folder

## Supported Runtime Environment
MetalFX currently requires an **Apple Silicon Mac** or a **MetalFX-supported iPhone / iPad device**, such as **A17 or later devices**.
Devices that do not support MetalFX will not be able to run this plugin properly.


## Console Commands

| Command | Parameters | Description |
| --- | --- | --- |
| `r.MetalFX.Enable` | bool (0,1) | Enable / Disable MetalFX. |
