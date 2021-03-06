/**
 * \file dnn/src/arm_common/conv_bias/fp32/f32_direct_nchw_nchw44_kern.h
 * MegEngine is Licensed under the Apache License, Version 2.0 (the "License")
 *
 * Copyright (c) 2014-2020 Megvii Inc. All rights reserved.
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT ARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.
 */
#pragma once
#include "src/arm_common/conv_bias/intrinsic_helper.h"
#include "src/arm_common/conv_bias/opr_impl.h"
#include "src/arm_common/elemwise_op.h"
#include "src/arm_common/simd_macro/marm_neon.h"
#include "src/common/unroll_macro.h"
#include "src/common/utils.h"
#include "src/fallback/conv_bias/common.h"

namespace megdnn {
namespace arm_common {
namespace {
/**
 *\brief ShiftCalHelper is core calculate code
 *\tparam src_idx is offset for src regs
 *\tparam weight_idx is offset for weight regs
 *\tparam T is type of output regs
 *\tparam T2 is type of src regs
 *\tparam T3 is type of weight regs
 */
template <int src_idx, int weight_idx, int c_dim, typename Func, int stride,
          typename T, typename T2, typename T3>
struct ShiftCalHelper {
    static void impl(T& c, T2& src, T3& weight);
};

template <int src_idx, int weight_idx, typename Func, int stride, typename T,
          typename T2, typename T3>
struct ShiftCalHelper<src_idx, weight_idx, 2, Func, stride, T, T2, T3> {
    static void impl(T& c, T2& src, T3& weight) {
#define cb(step)                                                     \
    c[0][step] = Func::template impl<(step * stride + src_idx) % 4>( \
            c[0][step], weight[0][weight_idx],                       \
            src[(step * stride + src_idx) / 4]);                     \
    c[1][step] = Func::template impl<(step * stride + src_idx) % 4>( \
            c[1][step], weight[1][weight_idx],                       \
            src[(step * stride + src_idx) / 4]);

        UNROLL_CALL_RAW(8, cb);
#undef cb
    }
};
template <int src_idx, int weight_idx, typename Func, int stride, typename T,
          typename T2, typename T3>
struct ShiftCalHelper<src_idx, weight_idx, 1, Func, stride, T, T2, T3> {
    static void impl(T& c, T2& src, T3& weight) {
#define cb(step)                                                     \
    c[0][step] = Func::template impl<(step * stride + src_idx) % 4>( \
            c[0][step], weight[0][weight_idx],                       \
            src[(step * stride + src_idx) / 4]);

        UNROLL_CALL_RAW(8, cb);
#undef cb
    }
};

template <int src_idx, int weight_idx, int c_dim, typename FUNC, int stride,
          typename T, typename T2, typename T3>
inline void cal_helper(T& c, T2& src, T3& weight) {
    ShiftCalHelper<src_idx, weight_idx, c_dim, FUNC, stride, T, T2, T3>::impl(
            c, src, weight);
};
template <int oc>
struct OCHelper {
public:
    static const int val = -1;
};

template <>
struct OCHelper<4> {
public:
    static const int val = 1;
};

template <>
struct OCHelper<8> {
public:
    static const int val = 2;
};
/**
 *  oc8_ow8(m = 8, n = 8) and oc4_ow8(m = 4, n = 8) gemm like kernel
 **/
template <BiasMode bias_mode, typename Op, int remain_w, int filter_size,
          int oc_block, int stride, int ow_block>
struct KerNeonXXs2NchwNchw44FP32 {
    static void impl(const float32_t* src_ptr, const float32_t* weight_ptr,
                     const float32_t* bias_ptr, float32_t* dst_ptr, int ic,
                     int ih, int iw, int ld_dst_oc, const Op& op);
};
template <BiasMode bias_mode, typename Op, int remain_w, int oc_block,
          int stride, int ow_block>
struct KerNeonXXs2NchwNchw44FP32<bias_mode, Op, remain_w, 7, oc_block, stride,
                                 ow_block> {
    static void impl(const float32_t* src_ptr, const float32_t* weight_ptr,
                     const float32_t* bias_ptr, float32_t* dst_ptr, int ic,
                     int ih, int iw, int ld_dst_oc, const Op& op) {
        constexpr int loop_ic_step = 1;
        constexpr int filter_size = 7;
        constexpr int oc_step = 4;
        constexpr int simd_len = 4;
        constexpr int src_reg_size =
                (ow_block * stride + filter_size - stride + simd_len - 1) /
                simd_len;

        constexpr int ld_weight_fw = oc_step * filter_size;
        const int ld_weight_oc = oc_step * filter_size * filter_size * ic;
        const int ld_weight_ic = oc_step * filter_size * filter_size;
        const int ld_src_ic = ih * iw;
        constexpr int c_dim = OCHelper<oc_block>::val;
        float32x4_t c[c_dim][8];
        init_ocx_ow8<c_dim, bias_mode, 8>(c, bias_ptr, oc_step);

        for (int ic_idx = 0; ic_idx < ic; ic_idx += loop_ic_step) {
            float32x4_t src[src_reg_size];
            float32x4_t weight[c_dim][filter_size];

#define KERNEL_CB(step)                                               \
    load_helper<src_reg_size, 0, simd_len, 0, Vld1q_f32>(             \
            src, src_ptr + step * iw, 0);                             \
    load_helper<filter_size, 0, oc_step, c_dim, Vld1q_f32>(           \
            weight, weight_ptr + step * ld_weight_fw, ld_weight_oc);  \
    cal_helper<0, 0, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight); \
    cal_helper<1, 1, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight); \
    cal_helper<2, 2, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight); \
    cal_helper<3, 3, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight); \
    cal_helper<4, 4, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight); \
    cal_helper<5, 5, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight); \
    cal_helper<6, 6, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight);

            UNROLL_CALL_RAW(7, KERNEL_CB)
#undef KERNEL_CB

            src_ptr += ld_src_ic;
            weight_ptr += ld_weight_ic;
        }
        store_ocx_ow8_remain_static<c_dim, remain_w, Op>(c, op, dst_ptr,
                                                         ld_dst_oc);
    }
};
template <BiasMode bias_mode, typename Op, int remain_w, int oc_block,
          int stride, int ow_block>
struct KerNeonXXs2NchwNchw44FP32<bias_mode, Op, remain_w, 5, oc_block, stride,
                                 ow_block> {
    static void impl(const float32_t* src_ptr, const float32_t* weight_ptr,
                     const float32_t* bias_ptr, float32_t* dst_ptr, int ic,
                     int ih, int iw, int ld_dst_oc, const Op& op) {
        constexpr int loop_ic_step = 1;
        constexpr int filter_size = 5;
        constexpr int oc_step = 4;
        constexpr int simd_len = 4;
        constexpr int src_reg_size =
                (ow_block * stride + filter_size - stride + simd_len - 1) /
                simd_len;

        constexpr int ld_weight_fw = oc_step * filter_size;
        const int ld_weight_oc = oc_step * filter_size * filter_size * ic;
        const int ld_weight_ic = oc_step * filter_size * filter_size;
        const int ld_src_ic = ih * iw;
        constexpr int c_dim = OCHelper<oc_block>::val;
        float32x4_t c[c_dim][8];
        init_ocx_ow8<c_dim, bias_mode, 8>(c, bias_ptr, oc_step);

        for (int ic_idx = 0; ic_idx < ic; ic_idx += loop_ic_step) {
            float32x4_t src[src_reg_size];
            float32x4_t weight[c_dim][filter_size];

#define KERNEL_CB(step)                                               \
    load_helper<src_reg_size, 0, simd_len, 0, Vld1q_f32>(             \
            src, src_ptr + step * iw, 0);                             \
    load_helper<filter_size, 0, oc_step, c_dim, Vld1q_f32>(           \
            weight, weight_ptr + step * ld_weight_fw, ld_weight_oc);  \
    cal_helper<0, 0, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight); \
    cal_helper<1, 1, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight); \
    cal_helper<2, 2, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight); \
    cal_helper<3, 3, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight); \
    cal_helper<4, 4, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight);
            UNROLL_CALL_RAW(5, KERNEL_CB)
#undef KERNEL_CB

            src_ptr += ld_src_ic;
            weight_ptr += ld_weight_ic;
        }
        store_ocx_ow8_remain_static<c_dim, remain_w, Op>(c, op, dst_ptr,
                                                         ld_dst_oc);
    }
};

template <BiasMode bias_mode, typename Op, int remain_w, int oc_block,
          int stride, int ow_block>
struct KerNeonXXs2NchwNchw44FP32<bias_mode, Op, remain_w, 3, oc_block, stride,
                                 ow_block> {
    static void impl(const float32_t* src_ptr, const float32_t* weight_ptr,
                     const float32_t* bias_ptr, float32_t* dst_ptr, int ic,
                     int ih, int iw, int ld_dst_oc, const Op& op) {
        constexpr int loop_ic_step = 1;
        constexpr int filter_size = 3;
        constexpr int oc_step = 4;
        constexpr int simd_len = 4;
        constexpr int src_reg_size =
                (ow_block * stride + filter_size - stride + simd_len - 1) /
                simd_len;

        constexpr int ld_weight_fw = oc_step * filter_size;
        const int ld_weight_oc = oc_step * filter_size * filter_size * ic;
        const int ld_weight_ic = oc_step * filter_size * filter_size;
        const int ld_src_ic = ih * iw;
        constexpr int c_dim = OCHelper<oc_block>::val;
        float32x4_t c[c_dim][8];
        init_ocx_ow8<c_dim, bias_mode, 8>(c, bias_ptr, oc_step);

        for (int ic_idx = 0; ic_idx < ic; ic_idx += loop_ic_step) {
            float32x4_t src[src_reg_size];
            float32x4_t weight[c_dim][filter_size];
            // row 0
            load_helper<src_reg_size, 0, simd_len, 0, Vld1q_f32>(src, src_ptr,
                                                                 0);
            load_helper<filter_size, 0, oc_step, c_dim, Vld1q_f32>(
                    weight, weight_ptr, ld_weight_oc);
            cal_helper<0, 0, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight);
            cal_helper<1, 1, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight);
            cal_helper<2, 2, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight);

            // row 1
            load_helper<src_reg_size, 0, simd_len, 0, Vld1q_f32>(
                    src, src_ptr + iw, 0);
            load_helper<filter_size, 0, oc_step, c_dim, Vld1q_f32>(
                    weight, weight_ptr + 1 * ld_weight_fw, ld_weight_oc);
            cal_helper<0, 0, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight);
            cal_helper<1, 1, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight);
            cal_helper<2, 2, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight);

            // row 2
            load_helper<src_reg_size, 0, simd_len, 0, Vld1q_f32>(
                    src, src_ptr + 2 * iw, 0);
            load_helper<filter_size, 0, oc_step, c_dim, Vld1q_f32>(
                    weight, weight_ptr + 2 * ld_weight_fw, ld_weight_oc);
            cal_helper<0, 0, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight);
            cal_helper<1, 1, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight);
            cal_helper<2, 2, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight);

            src_ptr += ld_src_ic;
            weight_ptr += ld_weight_ic;
        }
        store_ocx_ow8_remain_static<c_dim, remain_w, Op>(c, op, dst_ptr,
                                                         ld_dst_oc);
    }
};

template <BiasMode bias_mode, typename Op, int remain_w, int oc_block,
          int stride, int ow_block>
struct KerNeonXXs2NchwNchw44FP32<bias_mode, Op, remain_w, 2, oc_block, stride,
                                 ow_block> {
    static void impl(const float32_t* src_ptr, const float32_t* weight_ptr,
                     const float32_t* bias_ptr, float32_t* dst_ptr, int ic,
                     int ih, int iw, int ld_dst_oc, const Op& op) {
        constexpr int loop_ic_step = 1;
        constexpr int filter_size = 2;
        constexpr int oc_step = 4;
        constexpr int simd_len = 4;
        constexpr int src_reg_size =
                (ow_block * stride + filter_size - stride + simd_len - 1) /
                simd_len;

        constexpr int ld_weight_fw = oc_step * filter_size;
        const int ld_weight_oc = oc_step * filter_size * filter_size * ic;
        const int ld_weight_ic = oc_step * filter_size * filter_size;
        const int ld_src_ic = ih * iw;
        constexpr int c_dim = OCHelper<oc_block>::val;
        float32x4_t c[c_dim][8];
        init_ocx_ow8<c_dim, bias_mode, 8>(c, bias_ptr, oc_step);

        for (int ic_idx = 0; ic_idx < ic; ic_idx += loop_ic_step) {
            float32x4_t src[src_reg_size];
            float32x4_t weight[c_dim][filter_size];
            // row 0
            load_helper<src_reg_size, 0, simd_len, 0, Vld1q_f32>(src, src_ptr,
                                                                 0);
            load_helper<filter_size, 0, oc_step, c_dim, Vld1q_f32>(
                    weight, weight_ptr, ld_weight_oc);
            cal_helper<0, 0, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight);
            cal_helper<1, 1, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight);

            // row 1
            load_helper<src_reg_size, 0, simd_len, 0, Vld1q_f32>(
                    src, src_ptr + iw, 0);
            load_helper<filter_size, 0, oc_step, c_dim, Vld1q_f32>(
                    weight, weight_ptr + 1 * ld_weight_fw, ld_weight_oc);
            cal_helper<0, 0, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight);
            cal_helper<1, 1, c_dim, Vfmaq_laneq_f32, stride>(c, src, weight);

            src_ptr += ld_src_ic;
            weight_ptr += ld_weight_ic;
        }
        store_ocx_ow8_remain_static<c_dim, remain_w, Op>(c, op, dst_ptr,
                                                         ld_dst_oc);
    }
};
void pack_weight_fp32_nchw_nchw44(const float32_t* in_ptr, float32_t* dst_ptr,
                                  const int oc, const int kh, const int kw,
                                  const int ic) {
    constexpr int oc_step = 4;
    const int filter_oc_stride = kh * kw * ic;
    const int filter_ic_stride = kh * kw * oc_step;
    for (int oc_idx = 0; oc_idx < oc; oc_idx += oc_step) {
        const float32_t* in_ptr_oc = in_ptr + oc_idx * filter_oc_stride;
        float32_t* dst_ptr_oc = dst_ptr + oc_idx * filter_oc_stride;
        for (int kh_idx = 0; kh_idx < kh; ++kh_idx) {
            for (int kw_idx = 0; kw_idx < kw; ++kw_idx) {
                for (int ic_idx = 0; ic_idx < ic; ++ic_idx) {
                    float32x4_t vsrc = vld1q_f32(in_ptr_oc);
                    vst1q_f32(dst_ptr_oc + ic_idx * filter_ic_stride, vsrc);
                    in_ptr_oc += oc_step;
                }
                dst_ptr_oc += oc_step;
            }
        }
    }
}

template <BiasMode bias_mode, typename Op, int filter_size, int stride>
static void conv_direct_fp32_nchw_nchw44(
        const float32_t* src, const float32_t* filter, const float32_t* bias,
        float32_t*, float32_t* dst, const int oc, const int ic, const int ih,
        const int iw, const int oh, const int oh_block, const int ow,
        const Op& op, const int, const int) {
    constexpr int fh = filter_size;
    constexpr int fw = filter_size;
    constexpr int ic_step = 1;
    constexpr int big_oc_step = 8;
    constexpr int oc_step = 4;
    constexpr int ih_step = 1;
    constexpr int oh_step = 1;
    constexpr int ow_step = 8;
    constexpr int stride_h = stride;
    constexpr int stride_w = stride;
    constexpr int pack_iw_len = 1;

    const int img_stride = oh * ow;
    const int ow_end = ow / ow_step * ow_step;
    const int ow_remain = ow - ow_end;
    const int oc_end = oc / big_oc_step * big_oc_step;
    const int oc_remain = oc - oc_end;
    const int ld_dst_oc = oc_step * img_stride;

    using remain_fun = std::function<void(
            const float32_t* src_ptr, const float32_t* weight_ptr,
            const float32_t* bias_ptr, float32_t* dst_ptr, int ic, int ih,
            int iw, int ld_dst_oc, const Op& op)>;
    remain_fun kern_big_oc_remain = nullptr;
    remain_fun kern_small_oc_remain = nullptr;

    switch (ow_remain) {
#define cb(step)                                                               \
    case step:                                                                 \
        kern_big_oc_remain =                                                   \
                KerNeonXXs2NchwNchw44FP32<bias_mode, Op, step, filter_size,    \
                                          big_oc_step, stride, ow_step>::impl; \
        kern_small_oc_remain =                                                 \
                KerNeonXXs2NchwNchw44FP32<bias_mode, Op, step, filter_size,    \
                                          oc_step, stride, ow_step>::impl;     \
        break;

        UNROLL_CALL_RAW(8, cb);
        default:
            megdnn_assert(0, "no remain %d for kern", ow_remain);
    }
    for (int oc_idx = 0; oc_idx < oc_end; oc_idx += big_oc_step) {
        const int weight_offset = oc_idx * ic * fh * fw;
        for (int oh_idx = 0; oh_idx < oh_block; oh_idx += oh_step) {
            for (int ow_idx = 0; ow_idx < ow_end; ow_idx += ow_step) {
                const int src_offset =
                        (oh_idx * stride_h * iw + ow_idx * stride_w * ih_step) *
                        ic_step * pack_iw_len;
                const int dst_offset =
                        oc_idx * img_stride + (oh_idx * ow + ow_idx) * oc_step;
                KerNeonXXs2NchwNchw44FP32<bias_mode, Op, ow_step, filter_size,
                                          big_oc_step, stride,
                                          ow_step>::impl(src + src_offset,
                                                         filter + weight_offset,
                                                         bias + oc_idx,
                                                         dst + dst_offset, ic,
                                                         ih, iw, ld_dst_oc, op);
            }
            if (ow_remain > 0) {
                const int src_offset =
                        (oh_idx * stride_h * iw + ow_end * stride_w * ih_step) *
                        ic_step * pack_iw_len;
                const int dst_offset =
                        oc_idx * img_stride + (oh_idx * ow + ow_end) * oc_step;
                kern_big_oc_remain(src + src_offset, filter + weight_offset,
                                   bias + oc_idx, dst + dst_offset, ic, ih, iw,
                                   ld_dst_oc, op);
            }
        }
    }
    if (oc_remain > 0) {
        int oc_idx = oc_end;
        const int weight_offset = oc_idx * ic * fh * fw;
        for (int oh_idx = 0; oh_idx < oh_block; oh_idx += oh_step) {
            for (int ow_idx = 0; ow_idx < ow_end; ow_idx += ow_step) {
                const int src_offset =
                        (oh_idx * stride_h * iw + ow_idx * stride_w * ih_step) *
                        ic_step * pack_iw_len;
                const int dst_offset =
                        oc_idx * img_stride + (oh_idx * ow + ow_idx) * oc_step;
                KerNeonXXs2NchwNchw44FP32<bias_mode, Op, ow_step, filter_size,
                                          oc_step, stride,
                                          ow_step>::impl(src + src_offset,
                                                         filter + weight_offset,
                                                         bias + oc_idx,
                                                         dst + dst_offset, ic,
                                                         ih, iw, ld_dst_oc, op);
            }
            if (ow_remain > 0) {
                const int src_offset =
                        (oh_idx * stride_h * iw + ow_end * stride_w * ih_step) *
                        ic_step * pack_iw_len;
                const int dst_offset =
                        oc_idx * img_stride + (oh_idx * ow + ow_end) * oc_step;
                kern_small_oc_remain(src + src_offset, filter + weight_offset,
                                     bias + oc_idx, dst + dst_offset, ic, ih,
                                     iw, ld_dst_oc, op);
            }
        }
    }
}
}  // namespace
}  // namespace arm_common
}  // namespace megdnn
// vim: syntax=cpp.doxygen
