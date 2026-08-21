/**
 * @file plugin.hpp
 * @author jinhu.chen (jinhu.chen@lynxi.com)
 * @brief
 * @version 0.1
 * @date 2022-09-09
 *
 * @copyright Copyright (c) 2022
 *
 */

#pragma once

#include "stream.hpp"
#include <lyn_api.h>

namespace lynsdk {
class Plugin {
    static void close(lynPlugin_t &p) {
        if (p) {
            CHECK_ERR(lynPluginUnregister(p));
            p = nullptr;
        }
    }
    CObject<lynPlugin_t> p { nullptr, close };

public:
    Plugin(const std::string &name) {
        CHECK_ERR(lynPluginRegister(&p.obj, name.c_str()));
    }
    void call(const Stream &s, const std::string &func_name, void *args = nullptr, uint32_t arg_size = 0) {
        CHECK_ERR(lynPluginRunAsync(s.get(), p.obj, func_name.c_str(), args, arg_size));
    }
    template <typename T>
    void call(const Stream &s, const std::string &func_name, const T &args) {
        CHECK_ERR(lynPluginRunAsync(s.get(), p.obj, func_name.c_str(), (void *)(&args), sizeof(T)));
    }
};
} // namespace lynsdk
