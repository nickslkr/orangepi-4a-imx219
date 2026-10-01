inline float px(__global const ushort *s, int x, int y, int w, int h)
{
    x = clamp(x, 0, w - 1);
    y = clamp(y, 0, h - 1);
    return (float)(s[y * w + x] & 1023) * (1.0f / 1023.0f);
}

inline float3 corr(float3 v)
{
    v *= (float3)(1.25f, 0.8125f, 1.59375f);
    v = clamp(v, 0.0f, 1.0f);
    return native_powr(v, (float3)(0.74074074f));
}

inline uchar y8(float3 p)
{
    float y = 0.299f*p.x + 0.587f*p.y + 0.114f*p.z;
    return convert_uchar_sat_rte(y * 255.0f);
}

__kernel void debayer_yuv420(__global const ushort *s,
                             __global uchar *dst,
                             int w, int h)
{
    int gx = get_global_id(0);
    int gy = get_global_id(1);

    int x = gx << 1;
    int y = gy << 1;

    if (x >= w || y >= h)
        return;

    float R00   = px(s,x,  y,  w,h);
    float G10   = px(s,x+1,y,  w,h);
    float G01   = px(s,x,  y+1,w,h);
    float B11   = px(s,x+1,y+1,w,h);

    float Gm10  = px(s,x-1,y,  w,h);
    float G0m1  = px(s,x,  y-1,w,h);

    float Bm1m1 = px(s,x-1,y-1,w,h);
    float B1m1  = px(s,x+1,y-1,w,h);
    float Bm11  = px(s,x-1,y+1,w,h);

    float R20   = px(s,x+2,y,  w,h);
    float G21   = px(s,x+2,y+1,w,h);

    float R02   = px(s,x,  y+2,w,h);
    float G12   = px(s,x+1,y+2,w,h);
    float R22   = px(s,x+2,y+2,w,h);

    float3 p00 = corr((float3)(
        R00,
        0.25f*(Gm10+G10+G0m1+G01),
        0.25f*(Bm1m1+B1m1+Bm11+B11)));

    float3 p10 = corr((float3)(
        0.5f*(R00+R20),
        G10,
        0.5f*(B1m1+B11)));

    float3 p01 = corr((float3)(
        0.5f*(R00+R02),
        G01,
        0.5f*(Bm11+B11)));

    float3 p11 = corr((float3)(
        0.25f*(R00+R20+R02+R22),
        0.25f*(G10+G01+G21+G12),
        B11));

    size_t ybase = 0;
    size_t usize = (size_t)w * h;
    size_t vsize = usize + ((size_t)w * h) / 4;

    dst[ybase + (size_t)y*w + x]       = y8(p00);
    dst[ybase + (size_t)y*w + x + 1]   = y8(p10);
    dst[ybase + (size_t)(y+1)*w + x]   = y8(p01);
    dst[ybase + (size_t)(y+1)*w + x+1] = y8(p11);

    float3 m = 0.25f * (p00 + p10 + p01 + p11);

    float u = -0.169f*m.x - 0.331f*m.y + 0.500f*m.z + 0.5f;
    float v =  0.500f*m.x - 0.419f*m.y - 0.081f*m.z + 0.5f;

    size_t uv = (size_t)gy * (w / 2) + gx;

    dst[usize + uv] =
        convert_uchar_sat_rte(clamp(u, 0.0f, 1.0f) * 255.0f);

    dst[vsize + uv] =
        convert_uchar_sat_rte(clamp(v, 0.0f, 1.0f) * 255.0f);
}
