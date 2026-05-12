#if METALFX_PLUGIN_ENABLED
//---Active When you need this--- (ex. You add other MetalCPP versions)
#if METALFX_METALCPP
#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define MTLFX_PRIVATE_IMPLEMENTATION
#endif
//-------------------------------
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <MetalFX/MetalFX.hpp>
#endif //METALFX_PLUGIN_ENABLED
