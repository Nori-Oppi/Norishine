#
# FFmpeg: pinned, checksum-verified prebuilt static libraries from LizardByte/build-deps.
#
# To update: change FFMPEG_BUILD_DEPS_TAG and the SHA256 values below (the release page
# lists a sha256 digest for every asset), then rebuild and run the encoder tests.
#
# Set FFMPEG_PREPARED_BINARIES to use a different FFmpeg tree with the same layout
# (include/, lib/*.a). FFMPEG_PLATFORM_LIBRARIES can be overridden the same way.
#
include_guard(GLOBAL)

set(FFMPEG_BUILD_DEPS_TAG "v2026.1017.131201")
set(FFMPEG_SHA256_Windows-AMD64 "5e644e2446ceb6c243eefb88788eadcad454a73f30410f1107c036bf2ad86916")
set(FFMPEG_SHA256_Windows-ARM64 "289257f1540a12e98b67019756302b785f0d239d234ff7a811313d0487a0c1a8")
set(FFMPEG_SHA256_Linux-x86_64 "b2de5119cb0ab89cb6038acb75299b213f97a0c79d34557068add639511f8815")
set(FFMPEG_SHA256_Linux-aarch64 "fa6ca8f716922ebd4be222647bf972ec050af145406186787c43bfa0353f0a1d")
set(FFMPEG_SHA256_Linux-ppc64le "9cad0f5fe2cbe7c411b55134d26630c8fdda0f3323483d49b6537f553f58e554")
set(FFMPEG_SHA256_Darwin-arm64 "cf4acfcac2066b780a24834971954dae3285b961b1cf9300761920d9eac4073b")
set(FFMPEG_SHA256_Darwin-x86_64 "71367b4e6b6a8f7232b3421d255fc5b9c3625fc91a13398289b96be17c075047")
set(FFMPEG_SHA256_FreeBSD-amd64 "fe2eb089e2ae8aecf6ffba30cb056ff23371f9aecf1830c034f9b04d10643a44")

if(NOT DEFINED FFMPEG_PLATFORM_LIBRARIES)
    if(WIN32)
        set(FFMPEG_PLATFORM_LIBRARIES mfplat ole32 strmiids mfuuid vpl uuid bcrypt)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
        set(FFMPEG_PLATFORM_LIBRARIES va va-drm va-x11 X11)
    elseif(UNIX AND NOT APPLE)
        set(FFMPEG_PLATFORM_LIBRARIES numa va va-drm va-x11 X11)
    endif()
endif()

if(NOT DEFINED FFMPEG_PREPARED_BINARIES)
    set(_ffmpeg_platform "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
    if(NOT DEFINED "FFMPEG_SHA256_${_ffmpeg_platform}")
        message(FATAL_ERROR
                "No pinned FFmpeg package for ${_ffmpeg_platform}. "
                "Set FFMPEG_PREPARED_BINARIES to an FFmpeg tree with include/ and lib/.")
    endif()
    set(_ffmpeg_sha256 "${FFMPEG_SHA256_${_ffmpeg_platform}}")
    set(_ffmpeg_archive_name "${_ffmpeg_platform}-ffmpeg.tar.gz")
    set(_ffmpeg_dir "${CMAKE_BINARY_DIR}/_deps/ffmpeg-${FFMPEG_BUILD_DEPS_TAG}")
    set(_ffmpeg_archive "${_ffmpeg_dir}/${_ffmpeg_archive_name}")
    set(FFMPEG_PREPARED_BINARIES "${_ffmpeg_dir}/ffmpeg")

    if(NOT EXISTS "${FFMPEG_PREPARED_BINARIES}/lib/libavcodec.a")
        set(_ffmpeg_url
                "https://github.com/LizardByte/build-deps/releases/download/${FFMPEG_BUILD_DEPS_TAG}/${_ffmpeg_archive_name}")
        message(STATUS "Downloading FFmpeg ${FFMPEG_BUILD_DEPS_TAG} (${_ffmpeg_platform})")
        file(DOWNLOAD "${_ffmpeg_url}" "${_ffmpeg_archive}"
                EXPECTED_HASH SHA256=${_ffmpeg_sha256}
                TIMEOUT 600
                STATUS _ffmpeg_status)
        list(GET _ffmpeg_status 0 _ffmpeg_status_code)
        if(NOT _ffmpeg_status_code EQUAL 0)
            list(GET _ffmpeg_status 1 _ffmpeg_status_message)
            message(FATAL_ERROR "FFmpeg download failed: ${_ffmpeg_status_message}")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${_ffmpeg_archive}" DESTINATION "${_ffmpeg_dir}")
        file(REMOVE "${_ffmpeg_archive}")
        if(NOT EXISTS "${FFMPEG_PREPARED_BINARIES}/lib/libavcodec.a")
            message(FATAL_ERROR "FFmpeg archive did not contain ffmpeg/lib/libavcodec.a")
        endif()
    endif()
    message(STATUS "Using FFmpeg ${FFMPEG_BUILD_DEPS_TAG} at ${FFMPEG_PREPARED_BINARIES}")
else()
    message(STATUS "Using FFmpeg at ${FFMPEG_PREPARED_BINARIES}")
endif()

# The package ships newer NVENC headers than src/nvenc supports. GCC searches -I paths
# before -isystem ones, so they would shadow third-party/nv-codec-headers. Remove them
# until the NVENC code is updated (Sunshine fb3d85cf and follow-ups).
if(EXISTS "${FFMPEG_PREPARED_BINARIES}/include/ffnvcodec")
    file(REMOVE_RECURSE "${FFMPEG_PREPARED_BINARIES}/include/ffnvcodec")
endif()

# Order follows the package's `pkg-config --static` output; libraries a package does not
# ship are skipped. libvpl comes from the package when present, otherwise from the system.
set(FFMPEG_LIBRARIES "${FFMPEG_PREPARED_BINARIES}/lib/libavcodec.a")
foreach(_ffmpeg_lib dav1d opus SvtAv1Enc x264 x265 swresample swscale avutil cbs hdr10plus vpl)
    if(EXISTS "${FFMPEG_PREPARED_BINARIES}/lib/lib${_ffmpeg_lib}.a")
        list(APPEND FFMPEG_LIBRARIES "${FFMPEG_PREPARED_BINARIES}/lib/lib${_ffmpeg_lib}.a")
    endif()
endforeach()
list(APPEND FFMPEG_LIBRARIES ${FFMPEG_PLATFORM_LIBRARIES})

set(FFMPEG_INCLUDE_DIRS "${FFMPEG_PREPARED_BINARIES}/include")
