#include "typeConv.h"
#include <cmath>
#include <memory>
#include <stdio.h>

namespace COMMON {

int16_t expBitNum = 5;
int16_t baseBitNum = 15 - expBitNum;
int maxExp = 32;     // pow(2, expBitNum);
int maxBase = 1024;  // pow(2, baseBitNum);
int biasExp = 15;    // maxExp / 2 - 1;
int sig[2] = {1, -1};
float result = 5.96046e-08;

float half2float(int16_t ib) {
    int16_t s, e, m;
    s = (ib >> 15) & 0x1;
    e = (ib >> 10) & 0x1f;
    m = ib & 0x3ff;

    // added by puyang.wang@lynxi.com
    {
        if (0 == e)
            return sig[s] * m * result;
        else {
            union {
                unsigned int u32;
                float f32;
            } ou;

            e = (0x1f == e) ? 0xff : (e - 15 + 127);
            ou.u32 = (s << 31) | (e << 23) | (m << 13);
            return ou.f32;
        }
    }
}

int16_t float2half(float value) {
    int16_t ob;
    //            uint16 s,e,m;
    int16_t s, m;
    int e;  // modified by puyang.wang@lynxi。修正接近于0的值不能正确转换的bug。
    // int16_t expBitNum = 5;
    // int16_t baseBitNum = 15 - expBitNum;
    int maxExp = 32;     // pow(2,expBitNum);
    int maxBase = 1024;  // pow(2,baseBitNum);
    int biasExp = 15;    // maxExp/2 - 1;
    s = value < 0;
    double thd2;
    // thd1 = (maxBase-1)*1.0/maxBase * pow(2,(1 - biasExp));
    thd2 = 1.0 / maxBase * pow(2, (1 - biasExp));
    double x;
    bool inf_flag = 0;
    x = s ? -value : value;
    x = (x > 65504) ? 65504 : x;
    int16_t indA;
    indA = x < thd2 / 2;
    // indB = x > thd2/2;
    if (indA) {
        e = 0;
        m = 0;
        s = 0;
    }
    // if (indB)
    else  // float为Nan转为half
    {
        union {
            float xl;
            unsigned int u32;
        } ou;
        ou.xl = log2(x);

        if (((ou.u32 >> 23) & 0xff) == 0xff)  // float为inf转为half
        {
            e = maxExp - 1;
            if ((ou.u32 & 0x7fffff) == 0)
                inf_flag = 1;
            else {
                inf_flag = 0;
                s = (ou.u32 >> 31);
            }
        } else {
            e = biasExp + floor(ou.xl);
        }

        if (e > (maxExp - 1))
            printf("[double2uint16]Error: out of e range\n");
    }
    int16_t ind1, ind2;
    ind1 = e <= 0;
    ind2 = e > 0;
    if (ind1) {
        e = 0;
        m = round(x * pow(2, (biasExp - 1)) * maxBase);
    }
    if (ind2) {
        if (31 == e) {
            if (inf_flag)
                m = 0;
            else
                m = 1;
        } else {
            double xr;
            xr = x / pow(2, (e - biasExp)) - 1;
            m = round(xr * maxBase);
        }
    }

    ob = (s & 0x1) << 15 | (((e & 0x1f) << 10) + m);
    return ob;
}

}   // namespace COMMON