#include <iostream>
#include <filesystem>
#include <fstream>
#include <cassert>
#include <chrono>
#include <nanobind/nanobind.h>
#include <nanobind/stl/pair.h>
#include <nanobind/ndarray.h>

#define ONEVPL_EXPERIMENTAL

#include <vpl/mfx.h>
#include "vpl_header.hpp"

namespace fs = std::filesystem;
using namespace std;
using namespace chrono;

mfxSession global_session = nullptr;


mfxSession session_init();

pair<intptr_t, vector<unsigned short>> decode(const vector<mfxU8> &file_bin, mfxSession session);

nanobind::list helper_func(const nanobind::bytes &file_bin) {
    vector<mfxU8> input_data{static_cast<const mfxU8 *>(file_bin.data()),
                             static_cast<const mfxU8 *>(file_bin.data()) + file_bin.size()};
    if (global_session == nullptr) {
        global_session = session_init();
    }
    try {
        auto [array, sizes] = decode(input_data, global_session);
        auto result = nanobind::list();
//                ndarray.first,
//                ndarray.second,
//                nanobind::cast(sizes.first),
//                nanobind::cast(sizes.second)
//        );
        result.append(nanobind::int_(array));
        result.append(nanobind::int_(sizes.at(0)));
        result.append(nanobind::int_(sizes.at(1)));
        result.append(nanobind::int_(sizes.at(2)));
        return result;
    } catch (std::exception &exp) {
        cerr << exp.what() << endl;
        MFXVideoDECODE_Close(global_session);
        global_session = nullptr;
        throw exp;
    };
}

NB_MODULE(test_ext, m) {
    m.def("decode", helper_func);
}

mfxSession session_init() {
    mfxSession session = nullptr;
    auto loader = MFXLoad();
    auto mfx_cfg = MFXCreateConfig(loader);
    mfxVariant variant;
    variant.Type = MFX_VARIANT_TYPE_U32;
    variant.Data.U32 = MFX_IMPL_TYPE_HARDWARE;
    CHECK(MFXSetConfigFilterProperty(mfx_cfg, reinterpret_cast<const mfxU8 *>("mfxImplDescription.Impl"), variant));
//#ifdef _WIN32
//    variant.Data.U32 = MFX_ACCEL_MODE_VIA_D3D11;
//#elifdef __linux__
//    variant.Data.U32 = MFX_ACCEL_MODE_VIA_VAAPI;
//#else
//variant.Data.U32 = MFX_ACCEL_MODE_NA;
//#endif
    variant.Data.U32 = MFX_ACCEL_MODE_VIA_D3D11;
    CHECK(MFXSetConfigFilterProperty(mfx_cfg, reinterpret_cast<const mfxU8 *>("mfxImplDescription.AccelerationMode"),
                                     variant));
    variant.Data.U32 = MFX_CODEC_JPEG;
    CHECK(MFXSetConfigFilterProperty(mfx_cfg,
                                     reinterpret_cast<const mfxU8 *>("mfxImplDescription.mfxDecoderDescription.decoder.CodecID"),
                                     variant));
    mfxStatus iter;
    mfxImplDescription *implDesc;

    for (int k = 0; iter = MFXEnumImplementations(loader, k, mfxImplCapsDeliveryFormat::MFX_IMPLCAPS_IMPLDESCSTRUCTURE, reinterpret_cast<mfxHDL *>(&implDesc)), iter != MFX_ERR_NOT_FOUND; ++k) {
        switch (iter) {
            case MFX_ERR_NONE:
                DEBUG(cout << "Found suitable device: " << implDesc->ImplName << endl;)
                DEBUG(cout << "License: " << implDesc->License << endl;)
                DEBUG(cout << "Device ID: " << implDesc->Dev.DeviceID << endl;)
                DEBUG(cout << "Vendor ID: " << implDesc->Impl << endl;)

//                cout <<  << endl;
                MFXCreateSession(loader, k, &session);
                mfxIMPL impl;
                if (MFXQueryIMPL(session, &impl) == MFX_ERR_NONE) {
                    DEBUG(
                            switch (impl & 0x0f00) {
                                case MFX_IMPL_VIA_D3D11:
                                    cout << "Impl: MFX_IMPL_VIA_D3D11" << endl;
                                    break;
                                case MFX_IMPL_VIA_VAAPI:
                                    cout << "Impl: MFX_IMPL_VIA_VAAPI" << endl;
                                    break;
                                default:
                                    cout << "Impl: MFX_IMPL_UNSUPPORTED" << endl;
                            }
                    )
                }
            default:
                MFXDispReleaseImplDescription(loader, implDesc);

        }
    }
    if (!session) {
        cerr << "Cannot found suitable device." << endl;
        CHECK(MFX_ERR_NOT_FOUND);
    }
    MFXUnload(loader);

    return session;
}

pair<intptr_t, vector<unsigned short>> decode(const vector<mfxU8> &file_bin, mfxSession session) {
    auto file_size = file_bin.size();

    mfxBitstream bitstream = {nullptr};
    bitstream.MaxLength = file_size;
    bitstream.DataLength = file_size;
    bitstream.Data = static_cast<mfxU8 *>(calloc(bitstream.MaxLength, sizeof(mfxU8)));
    assert(bitstream.MaxLength >= bitstream.DataLength);
    bitstream.DataFlag = MFX_BITSTREAM_COMPLETE_FRAME;
    copy(file_bin.begin(), file_bin.end(), bitstream.Data);

    bitstream.CodecId = MFX_CODEC_JPEG;

    mfxVideoParam decodeParams = {};
    decodeParams.mfx.CodecId = MFX_CODEC_JPEG;
    decodeParams.IOPattern = MFX_IOPATTERN_OUT_VIDEO_MEMORY;

    CHECK(MFXVideoDECODE_DecodeHeader(session, &bitstream, &decodeParams));

    decodeParams.mfx.FrameInfo.ChromaFormat = decodeParams.mfx.JPEGChromaFormat;

    auto [height, width] = *size_from_header(bitstream.Data, bitstream.DataLength);
    switch (CHECK(MFXVideoDECODE_Init(session, &decodeParams))) {
        case MFX_ERR_NONE:
            break;
        default:
            MFXVideoDECODE_Close(session);
    }

    mfxFrameSurface1 *surface_out = nullptr;
    mfxSyncPoint syncPoint;

    auto start_jpeg_dec = system_clock::now();
    CHECK(MFXVideoDECODE_DecodeFrameAsync(session, &bitstream, nullptr, &surface_out, &syncPoint));
    mfxStatus sync_sts = MFX_WRN_IN_EXECUTION;
    while (sync_sts == MFX_WRN_IN_EXECUTION) {
        switch (sync_sts = surface_out->FrameInterface->Synchronize(surface_out, 1)) {
            case MFX_WRN_IN_EXECUTION:
                goto skip;
                break;
            default:
                check(sync_sts, __LINE__ - 4);
        }
    }
    skip:
    DEBUG(cout << "decode JPEG: " << duration_cast<microseconds>((system_clock::now() - start_jpeg_dec)).count() << "us" << endl;)
    CHECK(surface_out->FrameInterface->Map(surface_out, MFX_MAP_READ));

    free(bitstream.Data);

    auto data_ptr = surface_out->Data;
    auto pitch = data_ptr.Pitch;

    auto output_mem = reinterpret_cast<unsigned char *>(malloc(sizeof(mfxU8) * (size_t) ((surface_out->Info.CropH * pitch) * 1.5)));
    copy(surface_out->Data.Y, surface_out->Data.Y + surface_out->Info.CropH * pitch, output_mem);
    copy(surface_out->Data.UV, surface_out->Data.UV + surface_out->Info.CropH / 2 * pitch,
         output_mem + surface_out->Info.CropH * pitch);
//    for (int i = 0; i < surface_out->Info.CropH; ++i) {
//        copy(surface_out->Data.Y + i * pitch, surface_out->Data.Y + i * pitch + surface_out->Info.CropW, output_mem + i * surface_out->Info.CropW);
//    }
//    for (int i = 0; i < surface_out->Info.CropH / 2; ++i) {
//        copy(surface_out->Data.UV + i * pitch, surface_out->Data.UV + i * pitch + surface_out->Info.CropW,
//             output_mem + surface_out->Info.CropW * surface_out->Info.CropH + i * surface_out->Info.CropW);
//    }
//    auto Y_arr = nanobind::ndarray(static_cast<void *>(output_mem), {surface_out->Info.CropH, surface_out->Info.CropW}, {}, initializer_list<int64_t>{pitch, sizeof(mfxU8) * 1},
//                                   nanobind::dtype<uint8_t>(), nanobind::device::cpu::value, 0, 'C');
//    auto UV_arr = nanobind::ndarray(static_cast<void *>(output_mem + surface_out->Info.CropW * surface_out->Info.CropH), {surface_out->Info.CropH / 2UL, surface_out->Info.CropW}, {},
//                                    initializer_list<int64_t>{pitch, sizeof(mfxU8) * 1}, nanobind::dtype<uint8_t>(), nanobind::device::cpu::value, 0, 'C');

    DEBUG(printf("width: %d\n", width);)
    DEBUG(printf("height: %d\n", height);)
    DEBUG(printf("pitch: %d\n", pitch);)

    CHECK(surface_out->FrameInterface->Unmap(surface_out));
    CHECK(surface_out->FrameInterface->Release(surface_out));


    MFXVideoDECODE_Close(session);
//    MFXUnload(loader);
//    auto ndarray_pair = make_pair(Y_arr, UV_arr);
//    auto size_pair = make_pair(height, width);
    return make_pair(reinterpret_cast<intptr_t>(output_mem), vector{height, width, pitch});
}