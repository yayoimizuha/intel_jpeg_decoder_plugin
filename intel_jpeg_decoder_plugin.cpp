#include <iostream>
#include <filesystem>
#include <fstream>
#include <cassert>
#include <chrono>
#include <omp.h>

#define ONEVPL_EXPERIMENTAL

#include <vpl/mfx.h>
#include "vpl_header.hpp"

namespace fs = std::filesystem;
using namespace std;
using namespace chrono;

void decode_test(const fs::path &file_path, mfxSession session);

mfxSession session_init();

int main() {
    cout << "Hello, World!" << endl;
    mfxSession session = nullptr;
    fs::path root_directory_path = R"(E:\helloproject-ai-data\blog_images)";
    omp_set_num_threads(1);
//    auto directory_iterator = fs::directory_iterator(root_directory_path);
//    auto member_dirs = vector<fs::directory_entry>(begin(directory_iterator), end(directory_iterator));
//#pragma omp parallel for private(session)
//    for (int i = 0; i < member_dirs.size(); ++i) {
//        const auto &member_dir = member_dirs.at(i);
//        session = nullptr;
//    }
    for (const auto &member_dir: fs::directory_iterator(root_directory_path)) { // NOLINT(*-loop-convert)
        cout << member_dir.path().string() << endl;
        int counter = 0;
        auto beg = system_clock::now();
        auto sub_directory_iterator = fs::directory_iterator(member_dir);
        auto pic_files = vector<fs::directory_entry>(begin(sub_directory_iterator), end(sub_directory_iterator));
//#pragma omp parallel for firstprivate(session) reduction(+:counter)
        for (int j = 0; j < pic_files.size(); ++j) { // NOLINT(*-loop-convert)
            const auto &pic_file = pic_files.at(j);
            DEBUG(cout << pic_file.path().string() << endl;)
            counter++;
            try {
                if (session == nullptr) {
                    session = session_init();
                } //initiate session
                decode_test(pic_file, session);
            } catch (std::exception &exception) {
                cerr << exception.what() << endl;
                MFXVideoDECODE_Close(session);
                session = nullptr;
            }
        }
        cout << counter << " file(s) in :" << member_dir << endl;
        cout << static_cast<double>(duration_cast<milliseconds>((system_clock::now() - beg)).count()) / counter << "ms/pcs" << endl;
        if (member_dir.path().filename().string() == "ブログ")break;
//        if (i > 1)break;
    }
    return 0;
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

void decode_test(const fs::path &file_path, mfxSession session) {
    DEBUG(cout << "decode:" << file_path.string() << endl;)

    ifstream file_descriptor(file_path, ios::binary | ios::ate);
    if (!file_descriptor) {
        std::cerr << "File open error: " << file_path << std::endl;
    }
    auto file_size = file_descriptor.tellg();
    file_descriptor.seekg(0, ios::beg);

    mfxBitstream bitstream = {nullptr};
    bitstream.MaxLength = file_size;
    bitstream.DataLength = file_size;
    bitstream.Data = static_cast<mfxU8 *>(calloc(bitstream.MaxLength, sizeof(mfxU8)));
    assert(bitstream.MaxLength >= bitstream.DataLength);
    bitstream.DataFlag = MFX_BITSTREAM_COMPLETE_FRAME;
    if (file_descriptor.read(reinterpret_cast<char *>(bitstream.Data), bitstream.DataLength)) {
        DEBUG(cout << "File read success: " << file_path << " : " << file_size << " bytes" << endl;)
    } else {
        cout << "File read error: " << file_path << endl;
    }
    file_descriptor.close();
    bitstream.CodecId = MFX_CODEC_JPEG;

    mfxVideoParam decodeParams = {};
    decodeParams.mfx.CodecId = MFX_CODEC_JPEG;
    decodeParams.IOPattern = MFX_IOPATTERN_OUT_VIDEO_MEMORY;

    CHECK(MFXVideoDECODE_DecodeHeader(session, &bitstream, &decodeParams));

//    decodeParams.mfx.FrameInfo.FourCC = MFX_FOURCC_RGB4;
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
//                cout << file_path << endl;
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
    auto output_path = fs::path(R"(C:\Users\tomokazu\CLionProjects\intel_jpeg_decoder_plugin\dest)") / file_path.filename();
    auto ext = ".jpg"s;
    auto ext_pos = output_path.string().rfind(ext);
    if (ext_pos != string::npos) {
        output_path = output_path.string().replace(ext_pos, ext.length(), "_"s + to_string(width) + "x" + to_string(height) + "_"s + to_string(pitch) + ".bgra"s);
    }
//    ofstream output_file(output_path, ios::binary | ios::out);
//    if (!output_file) {
//        std::cerr << "Output file open error: " << file_path << std::endl;
//    }
//    output_file.write(reinterpret_cast<char *>(data_ptr.B), pitch * height);
//    output_file.write(reinterpret_cast<char *>(data_ptr.Y), pitch * height);
//    output_file.write(reinterpret_cast<char *>(data_ptr.UV), pitch * (height / 2));
//    output_file.flush();
//    output_file.close();
    DEBUG(printf("width: %d\n", width);)
    DEBUG(printf("height: %d\n", height);)
    DEBUG(printf("pitch: %d\n", pitch);)

    CHECK(surface_out->FrameInterface->Unmap(surface_out));
    CHECK(surface_out->FrameInterface->Release(surface_out));


    MFXVideoDECODE_Close(session);
//    MFXUnload(loader);
}