# Settings

Every setting in the web UI (**Settings**) and in Norilight's **Host Settings**, by page.
In the web UI, hover a setting's name for its explanation; search covers everything.
**Advanced** settings only show with *Show advanced settings* on. The name in
`sunshine.conf` is given for people who edit the file directly.

Generated from the host's settings schema by `scripts/docs/settings_reference.py`.

Pages: [General](#general) · [Display](#display) · [Video](#video) · [Audio](#audio) · [Input](#input) · [Network and pairing](#network) · [Commands](#apps) · [Fixes for specific problems](#fixes) · [Files and certificates](#files) · [NVIDIA NVENC](#nv) · [Intel Quick Sync](#qsv) · [AMD AMF](#amd) · [Apple VideoToolbox](#vt) · [VA-API](#vaapi) · [Software encoder](#sw)

<a id="general"></a>

## General

Name, language and tray icon.

- **Host name**: The name your devices show for this PC. Leave empty to use the PC's name.
  <br>`sunshine_name` · default: empty
- **Language**: Language of this page and the tray menu.
  <br>`locale` · default: English
- **Show icon in the system tray**: Shows Norishine next to the clock, with shortcuts to this page.
  <br>`system_tray` · default: On
- **Hide Stop and Restart in the tray menu**: Useful when other people use this PC.
  <br>`hide_tray_controls` · default: Off · advanced
- **Tell me about beta versions**: Also notify you when a test version is released, not only full releases.
  <br>`notify_pre_releases` · default: Off · advanced

<a id="display"></a>

## Display

Which screen is streamed and what happens to your monitor.

**Virtual display**

- **Stream to a virtual display**: Each stream gets its own screen that matches the device's resolution and refresh rate, and your monitors are left alone. Turn off to stream one of your monitors instead. A virtual display is also used when no monitor is connected, or when an app or device asks for one.
  <br>`headless_mode` · default: Off · Windows only
- **Run the virtual display at double refresh rate**: Can reduce stutter on some PCs. The stream's frame rate doesn't change.
  <br>`double_refreshrate` · default: Off · advanced · Windows only
- **Keep the mouse on the virtual display**: Places the virtual display to the right of all your monitors so the mouse can't wander onto them.
  <br>`isolated_virtual_display_option` · default: Off · advanced · Windows only

**When streaming a monitor** (not used while streaming to a virtual display)

- **Monitor to stream**: Used when not streaming to a virtual display. Automatic uses the main display.
  <br>`output_name` · default: empty
- **Prepare the monitor when a stream starts**: Changes are undone when the stream ends. If you run more than one host on this PC, set this to "Don't change anything" in all of them.
  <br>`dd_configuration_option` · default: Don't change anything · Windows only
- **Change the monitor's resolution**: Switch the monitor to the device's resolution during the stream. Needs "Optimize game settings" on in Moonlight.
  <br>`dd_resolution_option` · default: Match the device · Windows only
- **Fixed resolution**: For example 1920x1080.
  <br>`dd_manual_resolution` · default: empty · Windows only
- **Change the monitor's refresh rate**: Switch the monitor to the device's frame rate during the stream.
  <br>`dd_refresh_rate_option` · default: Match the device · Windows only
- **Fixed refresh rate**: For example 60 or 119.88.
  <br>`dd_manual_refresh_rate` · default: empty · Windows only
- **Match the device's HDR**: Turn HDR on or off on the monitor to match the device.
  <br>`dd_hdr_option` · default: Match the device · Windows only
- **Replace requested resolutions**: Replace the resolution or refresh rate a client asks for with other values. Entries are checked from top to bottom and the first match is used. Leave a "Requested" field empty to match any value. Entries with a resolution only work when "Optimize game settings" is on in Moonlight.
  <br>`dd_mode_remapping` · advanced · Windows only
- **Wait before putting the monitor back**: After a stream ends, wait this long before undoing the changes, so switching apps doesn't flicker.
  <br>`dd_config_revert_delay` · default: 3000 ms · advanced · Windows only
- **Put the monitor back when every device disconnects**: Normally the changes are undone when the app closes. Turn on to undo them when the last device disconnects, even if the app keeps running.
  <br>`dd_config_revert_on_disconnect` · default: Off · advanced · Windows only

**Graphics card and fallback**

- **Graphics card**: Which graphics card captures and encodes, by name (for example "Radeon RX 580 Series"). Leave empty unless the PC has more than one and the wrong one is used.
  <br>`adapter_name` · default: empty · advanced · Windows, Linux only
- **Resolution when none is given**: Used when an app is started from this page, or a device doesn't send a resolution. For example 1920x1080x60.
  <br>`fallback_mode` · default: empty · advanced

<a id="video"></a>

## Video

Encoder, codecs and frame rate.

- **Bitrate limit**: Caps the stream's bitrate, whatever the device asks for. Usually best left at No limit; set the bitrate in Moonlight instead.
  <br>`max_bitrate` · default: No limit · advanced
- **Minimum frame rate**: When nothing on screen changes, keep sending at least this many frames per second. Automatic suits most devices.
  <br>`minimum_fps_target` · default: 0.0 fps · advanced
- **Capture at the stream's frame rate**: Don't capture faster than the device asked for. Turning it off can cause lag on some devices.
  <br>`limit_framerate` · default: On · advanced
- **Video encoder**: Automatic picks the best one. A graphics card you choose here must be the one your monitor is connected to.
  <br>`encoder` · default: Automatic · advanced
- **Screen capture method**: Automatic uses the first one that works. Try the other one if the stream is black or stutters.
  <br>`capture` · default: Automatic · advanced · Windows, Linux only
- **Offer HEVC (H.265)**: HEVC looks better than H.264 at the same bitrate. Automatic offers it when the graphics card supports it.
  <br>`hevc_mode` · default: Automatic · advanced
- **Offer AV1**: AV1 looks better than HEVC at the same bitrate, but only newer graphics cards and devices support it.
  <br>`av1_mode` · default: Automatic · advanced

<a id="audio"></a>

## Audio

Which sound device is streamed.

- **Send sound to the device**: Turn off to stream the picture only, for example when using the device as an extra monitor.
  <br>`stream_audio` · default: On
- **Audio device to stream**: Used when the device keeps sound playing on this PC, by name or device ID. Leave empty for the default device.
  <br>`audio_sink` · default: empty · advanced
- **Audio device while the PC is silent**: Used when the PC's speakers are muted for the stream. Leave empty for Steam Streaming Speakers.
  <br>`virtual_sink` · default: empty · advanced
- **Keep it as the default during streams**: Stops Windows switching the sound back to your speakers mid-stream.
  <br>`keep_sink_default` · default: On · advanced · Windows only
- **Follow changes to the default audio device**: If you change the default sound device during a stream, stream the new one.
  <br>`auto_capture_sink` · default: On · advanced · Windows only
- **Install Steam's surround sound driver**: If Steam is installed, adds Steam Streaming Speakers so 5.1 and 7.1 sound work and the PC can stay silent while streaming.
  <br>`install_steam_audio_drivers` · default: On · advanced · Windows only

<a id="input"></a>

## Input

Controllers, keyboard, mouse and touch, for all devices.

- **Allow controllers**: For all devices. You can also turn input on or off per device on the Devices page.
  <br>`controller` · default: On
- **Controller type games see**: Automatic uses a PlayStation controller when yours has motion sensors or a touchpad, and an Xbox controller otherwise.
  <br>`gamepad` · default: Automatic
- **Use a PlayStation controller for controllers with motion sensors**: Part of Automatic.
  <br>`motion_as_ds4` · default: On · advanced
- **Use a PlayStation controller for controllers with a touchpad**: Part of Automatic.
  <br>`touchpad_as_ds4` · default: On · advanced
- **Back button clicks the touchpad**: When a PlayStation controller is used, the Back/Select button presses the touchpad.
  <br>`ds4_back_as_touchpad_click` · default: On · advanced
- **Randomize virtual controller MAC**: Give each emulated DualSense controller a random MAC address. Turn this off if games should recognise the same controller every session.
  <br>`ds5_inputtino_randomize_mac` · default: On · advanced · Linux only
- **Hold Back for the Guide button**: Holding Back/Select this long presses Guide/PS, for controllers without that button.
  <br>`back_button_timeout` · default: Off · advanced
- **Rumble**: Send game vibration to the device's controller.
  <br>`forward_rumble` · default: On · advanced
- **Allow keyboards**: For all devices. You can also turn input on or off per device on the Devices page.
  <br>`keyboard` · default: On
- **Key repeat delay**: How long a key is held before it starts repeating.
  <br>`key_repeat_delay` · default: 500 ms · advanced
- **Key repeat rate**: Repeats per second while a key is held.
  <br>`key_repeat_frequency` · default: 24.9 per second · advanced
- **Send physical key positions**: Helps games that ignore keyboard input. Turn off if keys come out wrong with a non-US keyboard layout.
  <br>`always_send_scancodes` · default: On · advanced · Windows only
- **Right Alt acts as the Windows key**: For devices that can't send the Windows key.
  <br>`key_rightalt_to_key_win` · default: Off · advanced
- **Key remapping**: Turn a key the client sends into another key on the host. By default the generic Shift, Ctrl and Alt keys become their left-hand versions, which some games need.
  <br>`keybindings` · advanced
- **Allow mouse, touch and pen**: For all devices. You can also turn input on or off per device on the Devices page.
  <br>`mouse` · default: On
- **Smooth scrolling**: Pass fine scrolling from touchpads and smooth wheels. Turn off if old programs scroll too fast.
  <br>`high_resolution_scrolling` · default: On · advanced
- **Pen and touch**: Send pen and touch as real pen and touch input. Turn off for old programs that don't understand it; touch then moves the mouse.
  <br>`native_pen_touch` · default: On · advanced · Windows only

<a id="network"></a>

## Network and pairing

Pairing, finding this PC, ports and encryption.

- **Allow new devices to pair**: Turn off once your devices are paired, so nobody else can pair. Paired devices keep working.
  <br>`enable_pairing` · default: On
- **Let devices find this PC**: Turn off to hide the PC on the network. Devices then need its IP address.
  <br>`enable_discovery` · default: On · advanced
- **Open ports on the router automatically (UPnP)**: Needed to stream over the internet, unless you forward the ports yourself. The router must allow UPnP.
  <br>`upnp` · default: Off · advanced
- **IPv6**: Also accept connections over IPv6.
  <br>`address_family` · default: IPv4 only · advanced
- **Public IP address**: For streaming over the internet. Leave empty to detect it.
  <br>`external_ip` · default: empty · advanced
- **Base port**: The host uses this port and a few next to it; this page is on the port + 1. Devices that already know this PC will need it re-added.
  <br>`port` · default: 47989 · asks before saving
- **Who can open this page**: Only change this if you know you need to reach this page from outside your home.
  <br>`origin_web_ui_allowed` · default: This PC and your home network · asks before saving
- **Encrypt streams at home**: Encryption costs a little performance on slow devices.
  <br>`lan_encryption_mode` · default: Off · asks before saving
- **Encrypt streams over the internet**: Encryption costs a little performance on slow devices.
  <br>`wan_encryption_mode` · default: When the device supports it · asks before saving
- **End the stream after no contact for**: End a stream when the device hasn't been heard from for this long.
  <br>`ping_timeout` · default: 10000 ms · advanced
- **Error correction**: Extra data sent to repair lost packets. Raise it on unreliable Wi-Fi; it uses more bandwidth.
  <br>`fec_percentage` · default: 20% · advanced

<a id="apps"></a>

## Commands

Commands that run when apps start, devices connect, or a device asks.

- **Commands when any app starts and stops**: "Do" runs before an app starts, "Undo" after it closes. If a "Do" command fails, the app doesn't start.
  <br>`global_prep_cmd` · asks before saving
- **Commands when devices join and leave**: "Do" runs when the first device connects, "Undo" when the last one leaves. "Undo" doesn't run when the app closes; put that cleanup in the commands above.
  <br>`global_state_cmd` · asks before saving
- **Commands devices can run**: Shown in Moonlight's menu during a stream. A device also needs "Run server commands" on the Devices page.
  <br>`server_cmd` · asks before saving
- **Add a "controls only" app**: Adds an app that sends input to the PC without picture or sound, for example to use your phone as a keyboard while streaming to the TV.
  <br>`enable_input_only_mode` · default: Off · advanced

<a id="fixes"></a>

## Fixes for specific problems

Only change these to work around a particular problem.

- **Fix washed-out HDR colours**: If HDR looks washed out on a virtual display, turn HDR off and on again when a stream starts, with this pause in between. About 500 ms usually works. Makes streams start a little slower.
  <br>`dd_wa_hdr_toggle_delay` · default: Off · advanced · Windows only
- **Realtime GPU priority**: Turn off if the stream freezes when the GPU is nearly out of memory and hardware-accelerated GPU scheduling is on.
  <br>`nvenc_realtime_hags` · default: On · advanced · Windows only
- **Fixed quality level**: Only for encoders that can't keep to a bitrate (rare). Lower values look better and use more data.
  <br>`qp` · default: 28 · advanced
- **Start even if no encoder is found**: Lets the host start when its encoder check fails, for example with no monitor connected. Streams may then fail.
  <br>`ignore_encoder_probe_failure` · default: Off · advanced
- **Whole-number frame rate for scripts**: Gives scripts SUNSHINE_CLIENT_FPS as a whole number (60) instead of the exact rate. For older scripts written for Sunshine.
  <br>`envvar_compatibility_mode` · default: Off · advanced
- **Keep app order in older Moonlight versions**: Adds hidden characters to app names so older clients keep your order. Can garble names in some clients or scripts. Each device can also turn it off on the Devices page.
  <br>`legacy_ordering` · default: Off · advanced
- **Log detail**: How much the host writes to its log (see Troubleshooting). Only raise it when you're collecting logs for a problem.
  <br>`min_log_level` · default: Normal · advanced

<a id="files"></a>

## Files and certificates

Where the host keeps its files. Changing these can break pairing.

- **Apps file**: Where the app list is saved.
  <br>`file_apps` · default: empty · asks before saving
- **State file**: Where the host's state used to be kept in one file (Apollo's sunshine_state.json). Norishine keeps it in a "state" folder next to this path: the host's ID, paired devices and the web UI account. An existing file is moved there on start and kept as a .v0.bak copy.
  <br>`file_state` · default: empty · asks before saving
- **Web UI account file**: Keep the web UI username and password in a file of your choice instead of the state folder.
  <br>`credentials_file` · default: empty · asks before saving
- **Log file**: Where the log is written.
  <br>`log_path` · default: empty · asks before saving
- **Certificate key**: Used for this page and for pairing. Changing it means every device has to pair again.
  <br>`pkey` · default: empty · asks before saving
- **Certificate**: Used for this page and for pairing. Changing it means every device has to pair again.
  <br>`cert` · default: empty · asks before saving

## Encoder tuning

Only the page for the encoder your PC uses is listed in the web UI; the rest are behind *Show the other encoders*.

<a id="nv"></a>

### NVIDIA NVENC

Only used with an NVIDIA graphics card.

- **Speed or quality**: Slower settings look better at the same bitrate but add latency. Raising the bitrate in Moonlight usually helps more.
  <br>`nvenc_preset` · default: Fastest · advanced · Windows, Linux only
- **Two-pass encoding**: A quick first pass keeps the stream to its bitrate and avoids packet loss.
  <br>`nvenc_twopass` · default: Quarter resolution · advanced · Windows, Linux only
- **Better flat areas at low bitrate (Spatial AQ)**: Spends more data on flat areas such as skies. Helps at low bitrates.
  <br>`nvenc_spatial_aq` · default: Off · advanced · Windows, Linux only
- **Allow larger frames**: Lets single frames go over the average size by this much, which improves fast motion. Too high can cause packet loss. 0 keeps it strict.
  <br>`nvenc_vbv_increase` · default: 0% · advanced · Windows, Linux only
- **Keep the GPU at full speed while streaming**: Lower latency, more power use.
  <br>`nvenc_latency_over_power` · default: On · advanced · Windows only
- **Capture OpenGL and Vulkan games at full frame rate**: Changes an NVIDIA driver setting while Norishine runs, and undoes it when it exits.
  <br>`nvenc_opengl_vulkan_on_dxgi` · default: On · advanced · Windows only
- **Simpler H.264 for very old devices (CAVLC)**: Needs about 10% more bitrate for the same quality.
  <br>`nvenc_h264_cavlc` · default: Off · advanced · Windows, Linux only
- **Gradual refresh**: Some clients, such as the Xbox app, need this to show the picture correctly.
  <br>`nvenc_intra_refresh` · default: Off · advanced · Windows, Linux only

<a id="qsv"></a>

### Intel Quick Sync

Only used with Intel graphics.

- **Speed or quality**: Slower presets give better quality at the same bitrate but add encoding latency.
  <br>`qsv_preset` · default: Medium · advanced · Windows, Linux only
- **H.264 entropy coding**: Entropy coding for H.264. CABAC gives better quality; CAVLC is easier to decode on old devices.
  <br>`qsv_coder` · default: Automatic · advanced · Windows, Linux only
- **Allow HEVC on older Intel graphics**: Uses more GPU and can stutter.
  <br>`qsv_slow_hevc` · default: Off · advanced · Windows, Linux only

<a id="amd"></a>

### AMD AMF

Only used with an AMD graphics card.

- **Encoding profile**: The base settings the options below adjust. Ultra low latency suits game streaming.
  <br>`amd_usage` · default: Ultra low latency · advanced · Windows only
- **Bitrate control**: How the encoder keeps to the bitrate.
  <br>`amd_rc` · default: Variable, latency-limited · advanced · Windows only
- **Strict bitrate limit (HRD)**: Fewer bitrate spikes, but can reduce quality on some cards.
  <br>`amd_enforce_hrd` · default: Off · advanced · Windows only
- **Speed or quality**: This controls the balance between encoding speed and quality.
  <br>`amd_quality` · default: Balanced · advanced · Windows only
- **Pre-analysis**: Better quality, more latency.
  <br>`amd_preanalysis` · default: Off · advanced · Windows only
- **Adaptive quality (VBAQ)**: Spends more data on smooth areas, where artifacts show most.
  <br>`amd_vbaq` · default: On · advanced · Windows only
- **H.264 entropy coding**: Allows you to select the entropy encoding to prioritize quality or encoding speed. H.264 only.
  <br>`amd_coder` · default: Automatic · advanced · Windows only

<a id="vt"></a>

### Apple VideoToolbox

- **H.264 entropy coding**: Entropy coding for H.264. CABAC gives better quality; CAVLC is easier to decode on old devices.
  <br>`vt_coder` · default: Automatic · advanced · macOS only
- **Software encoding**: Whether VideoToolbox may use software encoding when hardware encoding isn't available.
  <br>`vt_software` · default: Automatic · advanced · macOS only
- **Realtime encoding**: Ask VideoToolbox for real-time encoding, which keeps latency low. Turning it off can improve quality but adds latency.
  <br>`vt_realtime` · default: On · advanced · macOS only

<a id="vaapi"></a>

### VA-API

- **Strict bitrate buffer**: Fixes stutter with some AMD graphics on Linux.
  <br>`vaapi_strict_rc_buffer` · default: Off · advanced · Linux only

<a id="sw"></a>

### Software encoder

Only used when the CPU encodes the stream.

- **CPU threads**: How many CPU threads the CPU encoder uses at least. Raise it if CPU encoding can't keep up.
  <br>`min_threads` · default: 2 · advanced
- **Speed or quality**: Slower presets look better but need much more CPU.
  <br>`sw_preset` · default: Superfast · advanced
- **Tune for**: Zero latency is right for game streaming.
  <br>`sw_tune` · default: Zero latency: fast, low-latency streaming · advanced
