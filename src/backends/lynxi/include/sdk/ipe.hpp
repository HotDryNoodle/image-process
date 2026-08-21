/**
 * @file ipe.hpp
 * @author jinhu.chen (jinhu.chen@lynxi.com)
 * @brief ipe接口封装和增强
 * @version 0.1
 * @date 2022-07-20
 *
 * @copyright Copyright (c) 2022
 *
 */

#pragma once

#include "error.hpp"
#include "memory.hpp"
#include "stream.hpp"
#include <memory>

namespace lynsdk {
/**
 * @brief 封装了ipe接口
 *
 */
class Ipe {
    CObject<lynIpeConfigDesc_t> config { nullptr, [](lynIpeConfigDesc_t &p) {
                                            if (p) {
                                                CHECK_ERR(lynIpeDestroyConfigDesc(p));
                                                p = nullptr;
                                            }
                                        } };

    static void close_pic(lynIpePicDesc_t &p) {
        if (p) {
            CHECK_ERR(lynIpeDestroyPicDesc(p));
            p = nullptr;
        }
    }
    CObject<lynIpePicDesc_t> in { nullptr, close_pic };
    CObject<lynIpePicDesc_t> out { nullptr, close_pic };
    void reset_config() const {
        CHECK_ERR(lynIpeResetPicDesc(in.obj));
        CHECK_ERR(lynIpeResetPicDesc(out.obj));
        CHECK_ERR(lynIpeResetConfigDesc(config.obj));
    }
    Ipe &operator=(const Ipe &) = delete;
    Ipe(const Ipe &) = delete;

public:
    /**
     * @brief 配置ipe的参数
     *
     */
    class Config {
    public:
        virtual void config(ImageType &img_type, lynIpeConfigDesc_t cfg) const = 0;
        virtual int32_t get_passage_id() const {
            return 0;
        };
        virtual ~Config() = default;
        /**
         * @brief 计算输出图像类型
         *
         * @param input_type
         * @return ImageType
         */
        ImageType calc_output_type(ImageType input_type) const {
            lynIpeConfigDesc_t cfg = nullptr;
            CHECK_ERR(lynIpeCreateConfigDesc(&cfg));
            config(input_type, cfg);
            CHECK_ERR(lynIpeDestroyConfigDesc(cfg));
            return input_type;
        }
    };
    Ipe() {
        CHECK_ERR(lynIpeCreatePicDesc(&in.obj));
        CHECK_ERR(lynIpeCreatePicDesc(&out.obj));
        CHECK_ERR(lynIpeCreateConfigDesc(&config.obj));
    }
    /**
     * @brief 配置config,并执行ipe处理
     *
     * @param s
     * @param ipe_config
     * @param img
     * @param data
     */
    ImageType process(const Stream &s, const Config &ipe_config, const Image<LynData> &img, const LynData &data) const {
        reset_config();
        CHECK_ERR(lynIpeSetInputPicDesc(in.obj, (void *)(img.data.pointer()), img.w, img.h, img.fmt));
        CHECK_ERR(lynIpeSetOutputPicData(out.obj, data.pointer()));
        ImageType output_type = img;
        ipe_config.config(output_type, config.obj);
        CHECK_ERR(lynIpeCalOutputPicDesc(out.obj, in.obj, config.obj, ipe_config.get_passage_id()));
        CHECK_ERR(lynIpeProcessAsync(s.get(), in.obj, out.obj, config.obj));
        return output_type;
    }
    ImageType get_output_image_type(ImageType input_type, const Config &ipe_config) const {
        reset_config();
        static void *p = new char[1];
        CHECK_ERR(lynIpeSetInputPicDesc(in.obj, p, input_type.w, input_type.h, input_type.fmt));
        CHECK_ERR(lynIpeSetOutputPicData(out.obj, p));
        ipe_config.config(input_type, config.obj);
        CHECK_ERR(lynIpeCalOutputPicDesc(out.obj, in.obj, config.obj, ipe_config.get_passage_id()));
        CHECK_ERR(lynIpeGetPicDesc(out.obj, &p, &input_type.w, &input_type.h, &input_type.fmt));
        return input_type;
    }
};
} // namespace lynsdk

namespace lynsdk {
/**
 * @brief ipe相关操作
 *
 */
namespace ipe_ops {
/**
 * @brief 定义了ipe支持的操作类型
 *
 */
enum class Type {
    Crop,
    Flip,
    Resize,
    C2C,
    Pad,
    Mirror,
    Rotate,
};

/**
 * @brief 定义了图像操作对ImageType和config的修改接口
 *
 */
class Operation : public Ipe::Config {
public:
    /**
     * @brief 修改ImageType和config
     *
     * @param img_type 输入图像的类型,将被修改为输出图像类型
     * @param config 被修改的配置文件
     */
    virtual void config(ImageType &img_type, lynIpeConfigDesc_t config) const = 0;
    virtual ~Operation() = default;
    virtual Type get_type() const = 0;
};

/**
 * @brief 颜色空间转换
 *
 */
class C2C : public Operation {
    lynPixelFormat_t out;
    size_t standard;

public:
    C2C() = default;
    C2C(lynPixelFormat_t out, size_t standard = 0)
        : out(out)
        , standard(standard) {
    }
    void config(ImageType &img, lynIpeConfigDesc_t config) const override {
        CHECK_ERR(lynIpeSetC2CConfig(config, out, standard));
        img.fmt = out;
    }
    virtual ~C2C() = default;
    Type get_type() const override {
        return Type::C2C;
    }
};

/**
 * @brief 图像缩放
 *
 */
class Resize : public Operation {
    size_t w;
    size_t h;

public:
    Resize() = default;
    Resize(size_t w, size_t h)
        : w(w)
        , h(h) {};
    void config(ImageType &img, lynIpeConfigDesc_t config) const override {
        CHECK_ERR(lynIpeSetResizeConfig(config, w, h));
        img.w = w;
        img.h = h;
    };
    virtual ~Resize() = default;
    Type get_type() const override {
        return Type::Resize;
    }
};

/**
 * @brief 图像裁剪
 *
 */
class Crop : public Operation {
    size_t x;
    size_t y;
    size_t w;
    size_t h;

public:
    Crop() = default;
    Crop(size_t x, size_t y, size_t w, size_t h)
        : x(x)
        , y(y)
        , w(w)
        , h(h) {};
    void config(ImageType &img, lynIpeConfigDesc_t config) const override {
        CHECK_ERR(lynIpeSetCropConfig(config, x, y, w, h));
        img.w = w;
        img.h = h;
    };
    virtual ~Crop() = default;
    Type get_type() const override {
        return Type::Crop;
    }
};

/**
 * @brief 图像旋转
 *
 */
class Rotate : public Operation {
    struct Impl : public Ipe::Config {
        float angle;
        int32_t mode;
        uint8_t xColor;
        uint8_t yColor;
        uint8_t zColor;

        Impl(float angle, int32_t mode, uint8_t xColor, uint8_t yColor, uint8_t zColor)
            : angle(angle)
            , mode(mode)
            , xColor(xColor)
            , yColor(yColor)
            , zColor(zColor) {};
        void config(ImageType &, lynIpeConfigDesc_t config) const override {
            CHECK_ERR(lynIpeSetRotateConfig(config, angle, mode, xColor, yColor, zColor));
        };
        int32_t get_passage_id() const override {
            return 4;
        }
    };
    Impl impl;

public:
    Rotate() = default;
    Rotate(float angle, int32_t mode, uint8_t xColor, uint8_t yColor, uint8_t zColor)
        : impl(angle, mode, xColor, yColor, zColor) {};
    void config(ImageType &input_type, lynIpeConfigDesc_t config) const override {
        impl.config(input_type, config);
        if (impl.mode == 1) {
            input_type = Ipe().get_output_image_type(input_type, impl);
        }
    };
    int32_t get_passage_id() const override {
        return impl.get_passage_id();
    }
    virtual ~Rotate() = default;
    Type get_type() const override {
        return Type::Rotate;
    }
};

/**
 * @brief 图像填充
 *
 */
class Pad : public Operation {
    int32_t top;
    int32_t right;
    int32_t bottom;
    int32_t left;
    uint8_t xColor;
    uint8_t yColor;
    uint8_t zColor;

public:
    Pad() = default;
    Pad(int32_t top, int32_t right, int32_t bottom, int32_t left, uint8_t xColor, uint8_t yColor, uint8_t zColor)
        : top(top)
        , right(right)
        , bottom(bottom)
        , left(left)
        , xColor(xColor)
        , yColor(yColor)
        , zColor(zColor) {};
    void config(ImageType &img, lynIpeConfigDesc_t config) const override {
        CHECK_ERR(lynIpeSetPadConfig(config, top, right, bottom, left, xColor, yColor, zColor));
        img.h += top + bottom;
        img.w += left + right;
    };
    virtual ~Pad() = default;
    Type get_type() const override {
        return Type::Pad;
    }
};

/**
 * @brief 图像上下翻转
 *
 */
class Flip : public Operation {
public:
    Flip() = default;
    void config(ImageType &, lynIpeConfigDesc_t config) const override {
        CHECK_ERR(lynIpeEnableFlip(config));
    };
    virtual ~Flip() = default;
    Type get_type() const override {
        return Type::Flip;
    }
};

/**
 * @brief 图像左右翻转
 *
 */
class Mirror : public Operation {
public:
    Mirror() = default;
    void config(ImageType &, lynIpeConfigDesc_t config) const override {
        CHECK_ERR(lynIpeEnableMirror(config));
    };
    virtual ~Mirror() = default;
    Type get_type() const override {
        return Type::Mirror;
    }
};

/**
 * @brief Ipe的Passage
 *
 */
template <typename... Ops>
class Passage : public Ipe::Config {
public:
    const std::array<Type, 6> ResizeC2CPad { Type::Crop, Type::Flip, Type::Resize, Type::C2C, Type::Pad, Type::Mirror };
    const std::array<Type, 6> PadResizeC2C { Type::Crop, Type::Flip, Type::Pad, Type::Resize, Type::C2C, Type::Mirror };
    const std::array<Type, 6> C2CResizePad { Type::Crop, Type::Flip, Type::C2C, Type::Resize, Type::Pad, Type::Mirror };
    const std::array<Type, 6> PadC2CResize { Type::Crop, Type::Flip, Type::Pad, Type::C2C, Type::Resize, Type::Mirror };
    const std::array<Type, 1> Rotate { Type::Rotate };
    enum class Id { Unknown = -1, ResizeC2CPad, PadResizeC2C, C2CResizePad, PadC2CResize, Rotate };

private:
    // std::vector<std::unique_ptr<Operation>> ops;
    std::tuple<Ops...> ops;
    static const size_t OpsSize = std::tuple_size<std::tuple<Ops...>>::value;
    static_assert(OpsSize <= 6);
    std::array<Type, OpsSize> order;
    template <size_t N>
    static bool is_order_in(const std::array<Type, OpsSize> &order, const std::array<Type, N> &passage) {
        if (order.size() == 0 || order.size() > N) {
            return false;
        }
        auto passage_iter = passage.begin();
        for (auto &op : order) {
            while (passage_iter != passage.end() && op != *passage_iter) {
                passage_iter++;
            }
            if (passage_iter == passage.end()) {
                return false;
            }
            passage_iter++;
        }
        return true;
    }
    template <size_t N>
    std::enable_if_t<N == OpsSize, void> set_type(std::array<Type, OpsSize> &order) {
    }
    template <size_t N>
    std::enable_if_t<(N < OpsSize), void> set_type(std::array<Type, OpsSize> &order) {
        std::get<N>(order) = std::get<N>(ops).get_type();
        set_type<N + 1>(order);
    }
    Id calc_passage_id() {
        std::array<Type, OpsSize> order;
        set_type<0>(order);
        if (is_order_in(order, ResizeC2CPad)) {
            return Id::ResizeC2CPad;
        } else if (is_order_in(order, PadResizeC2C)) {
            return Id::PadResizeC2C;
        } else if (is_order_in(order, C2CResizePad)) {
            return Id::C2CResizePad;
        } else if (is_order_in(order, PadC2CResize)) {
            return Id::PadC2CResize;
        } else if (is_order_in(order, Rotate)) {
            return Id::Rotate;
        } else {
            return Id::Unknown;
        }
    }
    Id passage_id = Id::Unknown;
    template <size_t N>
    std::enable_if_t<(N < OpsSize), void> config(ImageType &img_type, lynIpeConfigDesc_t cfg) const {
        std::get<N>(ops).config(img_type, cfg);
        config<N + 1>(img_type, cfg);
    }
    template <size_t N>
    std::enable_if_t<N == OpsSize, void> config(ImageType &, lynIpeConfigDesc_t) const {
    }

public:
    /**
     * @brief 根据操作顺序生成Passage
     *
     * @exception std::invalid_argument 如果操作顺序不合法, 则抛出异常
     *
     * @param ops
     */
    Passage(Ops &&...ops)
        : ops(std::forward<Ops>(ops)...) {
        passage_id = calc_passage_id();
        if (passage_id == Id::Unknown) {
            throw std::invalid_argument("Passage is not valid");
        }
    }
    /**
     * @brief 获取passage id
     *
     * @return int32_t
     */
    int32_t get_passage_id() const override {
        return static_cast<int32_t>(passage_id);
    }
    /**
     * @brief config ipe
     *
     * @param img_type
     * @param cfg
     */
    void config(ImageType &img_type, lynIpeConfigDesc_t cfg) const override {
        config<0>(img_type, cfg);
    }
};

template <typename... Ops>
Passage<Ops...> make_passage(Ops &&...ops) {
    return Passage<Ops...>(std::forward<Ops>(ops)...);
}

template <typename... Ops>
std::unique_ptr<Ipe::Config> make_passage_ptr(Ops &&...ops) {
    return std::make_unique<Passage<Ops...>>(std::forward<Ops>(ops)...);
}

/**
 * @brief 中心剪裁
 *
 */
class CenterCrop : public Crop {
    double scale;
    double ratio;

public:
    CenterCrop(double scale, double ratio)
        : scale(scale)
        , ratio(ratio) {
    }
    void config(ImageType &img, lynIpeConfigDesc_t config) const override {
        size_t w = img.w * scale;
        size_t h = img.h * scale;
        w / h < ratio ? h = w / ratio : w = h * ratio;
        if (img.fmt == LYN_PIX_FMT_NV12) {
            w -= w % 2;
            h -= h % 2;
        }
        size_t x = (img.w - w) / 2;
        size_t y = (img.h - h) / 2;
        if (img.fmt == LYN_PIX_FMT_NV12) {
            x -= x % 2;
            y -= y % 2;
        }
        Crop(x, y, w, h).config(img, config);
    }
};

/**
 * @brief 将图像等比Resize, 且不会超出给定宽高
 *
 */
class Cover : public Resize {
    double w;
    double h;

public:
    Cover(size_t w, size_t h)
        : w(w)
        , h(h) {};
    void config(ImageType &img, lynIpeConfigDesc_t config) const override {
        double scale = img.w / w < img.h / h ? img.w / w : img.h / h;
        auto new_w = img.w / scale;
        auto new_h = img.h / scale;
        if (new_w > 1920) {
            new_w = 1920;
        }
        if (new_h > 1080) {
            new_h = 1080;
        }
        Resize(new_w, new_h).config(img, config);
    };
};

/**
 * @brief 将图像等比Resize, 且能填满给定宽高
 *
 */
class Contain : public Resize {
    double w;
    double h;

public:
    Contain(size_t w, size_t h)
        : w(w)
        , h(h) {};
    void config(ImageType &img, lynIpeConfigDesc_t config) const override {
        double scale = img.w / w > img.h / h ? img.w / w : img.h / h;
        auto new_w = img.w / scale;
        auto new_h = img.h / scale;
        if (new_w > 1920) {
            new_w = 1920;
        }
        if (new_h > 1080) {
            new_h = 1080;
        }
        Resize(new_w, new_h).config(img, config);
    };
};

/**
 * @brief 将图像对称填充至正方形
 *
 */
class Square : public Pad {
    uint8_t xColor;
    uint8_t yColor;
    uint8_t zColor;

public:
    Square(uint8_t xColor, uint8_t yColor, uint8_t zColor)
        : xColor(xColor)
        , yColor(yColor)
        , zColor(zColor) {};
    void config(ImageType &img, lynIpeConfigDesc_t config) const override {
        int32_t len = img.w < img.h ? img.h : img.w;
        auto pad_y = (len - img.h) / 2;
        auto pad_x = (len - img.w) / 2;
        Pad(pad_y, pad_x, pad_y, pad_x, xColor, yColor, zColor).config(img, config);
    };
};

/**
 * @brief 将图像对称填充至指定宽高
 *
 */
class SymmetricPad : public Pad {
    int32_t target_w;
    int32_t target_h;
    uint8_t xColor;
    uint8_t yColor;
    uint8_t zColor;

public:
    SymmetricPad(int w, int h, uint8_t xColor, uint8_t yColor, uint8_t zColor)
        : target_w(w) 
        , target_h(h)
        , xColor(xColor)
        , yColor(yColor)
        , zColor(zColor){};
        
    void config(ImageType &img, lynIpeConfigDesc_t config) const override {
        int pad_h = (target_h - img.h) / 2;
        int pad_w = (target_w - img.w) / 2;
        Pad(pad_h, pad_w, pad_h, pad_w, xColor, yColor, zColor).config(img, config);
    };
};
} // namespace ipe_ops
} // namespace lynsdk
