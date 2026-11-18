/**
 * @file src/nvhttp.cpp
 * @brief Definitions for the nvhttp (GameStream) server.
 */
// macros
#define BOOST_BIND_GLOBAL_PLACEHOLDERS

// standard includes
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

// lib includes
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/context_base.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <Simple-Web-Server/server_http.hpp>

// local includes
#include "adaptive.h"
#include "config.h"
#include "config_schema.h"
#include "display_device.h"
#include "file_handler.h"
#include "globals.h"
#include "host_stats.h"
#include "recommend.h"
#include "httpcommon.h"
#include "logging.h"
#include "manage_api.h"
#include "network.h"
#include "nvhttp.h"
#include "platform/common.h"
#include "process.h"
#include "rtsp.h"
#include "state_store.h"
#include "stream.h"
#include "system_tray.h"
#include "updater.h"
#include "utility.h"
#include "uuid.h"
#include "video.h"
#include "zwpad.h"
#include "version.h"

#ifdef _WIN32
  #include "platform/windows/virtual_display.h"
#endif

using namespace std::literals;

namespace nvhttp {

  static constexpr std::string_view EMPTY_PROPERTY_TREE_ERROR_MSG = "Property tree is empty. Probably, control flow got interrupted by an unexpected C++ exception. This is a bug in Sunshine. Moonlight-qt will report Malformed XML (missing root element)."sv;

  namespace fs = std::filesystem;
  namespace pt = boost::property_tree;

  using p_named_cert_t = crypto::p_named_cert_t;
  using PERM = crypto::PERM;

  struct client_t {
    std::vector<p_named_cert_t> named_devices;
  };

  struct pair_session_t;

  crypto::cert_chain_t cert_chain;
  static std::string one_time_pin;
  static std::string otp_passphrase;
  static std::string otp_device_name;
  static std::chrono::time_point<std::chrono::steady_clock> otp_creation_time;

  class SunshineHTTPSServer: public SimpleWeb::ServerBase<SunshineHTTPS> {
  public:
    SunshineHTTPSServer(const std::string &certification_file, const std::string &private_key_file):
        ServerBase<SunshineHTTPS>::ServerBase(443),
        context(boost::asio::ssl::context::tls_server) {
      // Disabling TLS 1.0 and 1.1 (see RFC 8996)
      context.set_options(boost::asio::ssl::context::no_tlsv1);
      context.set_options(boost::asio::ssl::context::no_tlsv1_1);
      context.use_certificate_chain_file(certification_file);
      context.use_private_key_file(private_key_file, boost::asio::ssl::context::pem);
    }

    std::function<bool(std::shared_ptr<Request>, SSL*)> verify;
    std::function<void(std::shared_ptr<Response>, std::shared_ptr<Request>)> on_verify_failed;

  protected:
    boost::asio::ssl::context context;

    void after_bind() override {
      if (verify) {
        context.set_verify_mode(boost::asio::ssl::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert | boost::asio::ssl::verify_client_once);
        context.set_verify_callback([](int verified, boost::asio::ssl::verify_context &ctx) {
          // To respond with an error message, a connection must be established
          return 1;
        });
      }
    }

    // This is Server<HTTPS>::accept() with SSL validation support added
    void accept() override {
      auto connection = create_connection(*io_service, context);

      acceptor->async_accept(connection->socket->lowest_layer(), [this, connection](const SimpleWeb::error_code &ec) {
        auto lock = connection->handler_runner->continue_lock();
        if (!lock) {
          return;
        }

        if (ec != SimpleWeb::error::operation_aborted) {
          this->accept();
        }

        auto session = std::make_shared<Session>(config.max_request_streambuf_size, connection);

        if (!ec) {
          boost::asio::ip::tcp::no_delay option(true);
          SimpleWeb::error_code ec;
          session->connection->socket->lowest_layer().set_option(option, ec);

          session->connection->set_timeout(config.timeout_request);
          session->connection->socket->async_handshake(boost::asio::ssl::stream_base::server, [this, session](const SimpleWeb::error_code &ec) {
            session->connection->cancel_timeout();
            auto lock = session->connection->handler_runner->continue_lock();
            if (!lock) {
              return;
            }
            if (!ec) {
              if (verify && !verify(session->request, session->connection->socket->native_handle())) {
                this->write(session, on_verify_failed);
              } else {
                this->read(session);
              }
            } else if (this->on_error) {
              this->on_error(session->request, ec);
            }
          });
        } else if (this->on_error) {
          this->on_error(session->request, ec);
        }
      });
    }
  };

  using https_server_t = SunshineHTTPSServer;
  using http_server_t = SimpleWeb::Server<SimpleWeb::HTTP>;

  struct conf_intern_t {
    std::string servercert;
    std::string pkey;
  } conf_intern;

  // uniqueID, session
  std::unordered_map<std::string, pair_session_t> map_id_sess;
  /// Guards map_id_sess and the OTP state. Recursive because a handler holding it calls
  /// helpers that take it too.
  std::recursive_mutex map_id_sess_mutex;
  client_t client_root;
  std::atomic<uint32_t> session_id_counter;

  using resp_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SunshineHTTPS>::Response>;
  using req_https_t = std::shared_ptr<typename SimpleWeb::ServerBase<SunshineHTTPS>::Request>;
  using resp_http_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response>;
  using req_http_t = std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request>;

  enum class op_e {
    ADD,  ///< Add certificate
    REMOVE  ///< Remove certificate
  };

  std::string get_arg(const args_t &args, const char *name, const char *default_value) {
    auto it = args.find(name);
    if (it == std::end(args)) {
      if (default_value != nullptr) {
        return std::string(default_value);
      }

      throw std::out_of_range(name);
    }
    return it->second;
  }

  // Helper function to extract command entries from a JSON object.
  cmd_list_t extract_command_entries(const nlohmann::json& j, const std::string& key) {
    cmd_list_t commands;

    // Check if the key exists in the JSON.
    if (j.contains(key)) {
      // Ensure that the value for the key is an array.
      try {
        for (const auto& item : j.at(key)) {
          try {
            // Extract "cmd" and "elevated" fields from the JSON object.
            std::string cmd = item.at("cmd").get<std::string>();
            bool elevated = util::get_non_string_json_value<bool>(item, "elevated", false);

            // Add the command entry to the list.
            commands.push_back({cmd, elevated});
          } catch (const std::exception& e) {
            BOOST_LOG(warning) << "Error parsing command entry: " << e.what();
          }
        }
      } catch (const std::exception &e) {
        BOOST_LOG(warning) << "Error retrieving key \"" << key << "\": " << e.what();
      }
    } else {
      BOOST_LOG(debug) << "Key \"" << key << "\" not found in the JSON.";
    }

    return commands;
  }

  void save_state() {
    // Never write state that could not be migrated (see state_store::init).
    if (!state_store::writable()) {
      return;
    }
    const auto &files = state_store::paths();

    client_t &client = client_root;
    nlohmann::json named_cert_nodes = nlohmann::json::array();

    std::unordered_set<std::string> unique_certs;
    std::unordered_map<std::string, int> name_counts;

    for (auto &named_cert_p : client.named_devices) {
      // Only add each unique certificate once.
      if (unique_certs.insert(named_cert_p->cert).second) {
        nlohmann::json named_cert_node = nlohmann::json::object();
        std::string base_name = named_cert_p->name;
        // Remove any pending id suffix (e.g., " (2)") if present.
        size_t pos = base_name.find(" (");
        if (pos != std::string::npos) {
          base_name = base_name.substr(0, pos);
        }
        int count = name_counts[base_name]++;
        std::string final_name = base_name;
        if (count > 0) {
          final_name += " (" + std::to_string(count + 1) + ")";
        }
        named_cert_node["name"] = final_name;
        named_cert_node["cert"] = named_cert_p->cert;
        named_cert_node["uuid"] = named_cert_p->uuid;
        named_cert_node["display_mode"] = named_cert_p->display_mode;
        named_cert_node["perm"] = static_cast<uint32_t>(named_cert_p->perm);
        named_cert_node["enable_legacy_ordering"] = named_cert_p->enable_legacy_ordering;
        named_cert_node["allow_client_commands"] = named_cert_p->allow_client_commands;
        named_cert_node["always_use_virtual_display"] = named_cert_p->always_use_virtual_display;

        // Add "do" commands if available.
        if (!named_cert_p->do_cmds.empty()) {
          nlohmann::json do_cmds_node = nlohmann::json::array();
          for (const auto &cmd : named_cert_p->do_cmds) {
            do_cmds_node.push_back(crypto::command_entry_t::serialize(cmd));
          }
          named_cert_node["do"] = do_cmds_node;
        }

        // Add "undo" commands if available.
        if (!named_cert_p->undo_cmds.empty()) {
          nlohmann::json undo_cmds_node = nlohmann::json::array();
          for (const auto &cmd : named_cert_p->undo_cmds) {
            undo_cmds_node.push_back(crypto::command_entry_t::serialize(cmd));
          }
          named_cert_node["undo"] = undo_cmds_node;
        }

        named_cert_nodes.push_back(named_cert_node);
      }
    }

    // Keep any other entries of the files (written by newer versions, for example).
    auto pairing = state_store::read(files.pairing);
    auto host = state_store::read(files.host);
    if (!pairing || !host) {
      return;  // Unreadable: leave it for the user to look at rather than overwrite it.
    }
    pairing->erase("devices");  // converted into named_devices when loaded
    (*pairing)["named_devices"] = named_cert_nodes;
    (*host)["uniqueid"] = http::unique_id;
    if (!state_store::write(files.pairing, *pairing)) {
      BOOST_LOG(error) << "Couldn't write "sv << files.pairing.string();
    }
    if (!state_store::write(files.host, *host)) {
      BOOST_LOG(error) << "Couldn't write "sv << files.host.string();
    }
  }

  /**
   * @brief Give the host a new unique ID and save it right away.
   *
   * Clients remember a host by this ID. Saving it only when a client paired meant a host
   * got a new ID on every start until then, and a client that had already seen it
   * refused it as an unexpected PC.
   */
  void generate_and_save_unique_id() {
    http::uuid = uuid_util::uuid_t::generate();
    http::unique_id = http::uuid.string();
    if (!config::sunshine.flags[config::flag::FRESH_STATE]) {
      save_state();
    }
  }

  void load_state() {
    const auto &files = state_store::paths();
    auto host = state_store::read(files.host);
    auto pairing = state_store::read(files.pairing);
    if (!host || !pairing || !state_store::writable()) {
      // Keep a valid ID for this run, but never overwrite files we could not read.
      if (http::unique_id.empty()) {
        http::uuid = uuid_util::uuid_t::generate();
        http::unique_id = http::uuid.string();
      }
      if (!host || !pairing) {
        return;
      }
    }

    // Without a stored unique ID (a new host) one is made, and saved once any paired
    // clients are loaded.
    const bool has_unique_id = host->contains("uniqueid") && (*host)["uniqueid"].is_string();
    if (has_unique_id) {
      std::string uid = (*host)["uniqueid"];
      http::uuid = uuid_util::uuid_t::parse(uid);
      http::unique_id = uid;
    }

    nlohmann::json root = *pairing;
    client_t client;  // Local client to load into

    // Import from the old format if available.
    if (root.contains("devices")) {
      for (auto &device_node : root["devices"]) {
        // For each device, if there is a "certs" array, add a named certificate.
        if (device_node.contains("certs")) {
          for (auto &el : device_node["certs"]) {
            auto named_cert_p = std::make_shared<crypto::named_cert_t>();
            named_cert_p->name = "";
            named_cert_p->cert = el.get<std::string>();
            named_cert_p->uuid = uuid_util::uuid_t::generate().string();
            named_cert_p->display_mode = "";
            named_cert_p->perm = PERM::_first_device;
            named_cert_p->enable_legacy_ordering = true;
            named_cert_p->allow_client_commands = true;
            named_cert_p->always_use_virtual_display = false;
            client.named_devices.emplace_back(named_cert_p);
          }
        }
      }
    }

    // Import from the new format.
    if (root.contains("named_devices")) {
      for (auto &el : root["named_devices"]) {
        auto named_cert_p = std::make_shared<crypto::named_cert_t>();
        named_cert_p->name = el.value("name", "");
        named_cert_p->cert = el.value("cert", "");
        named_cert_p->uuid = el.value("uuid", "");
        named_cert_p->display_mode = el.value("display_mode", "");
        // A missing "perm" means a state file from before permissions existed (Sunshine);
        // those clients could stream and send input, nothing more.
        named_cert_p->perm = (PERM)(util::get_non_string_json_value<uint32_t>(el, "perm", (uint32_t)PERM::_first_device)) & PERM::_all;
        named_cert_p->enable_legacy_ordering = el.value("enable_legacy_ordering", true);
        named_cert_p->allow_client_commands = el.value("allow_client_commands", true);
        named_cert_p->always_use_virtual_display = el.value("always_use_virtual_display", false);
        // Load command entries for "do" and "undo" keys.
        named_cert_p->do_cmds = extract_command_entries(el, "do");
        named_cert_p->undo_cmds = extract_command_entries(el, "undo");
        client.named_devices.emplace_back(named_cert_p);
      }
    }

    // Clear any existing certificate chain and add the imported certificates.
    cert_chain.clear();
    for (auto &named_cert : client.named_devices) {
      cert_chain.add(named_cert);
    }

    client_root = client;

    if (!has_unique_id) {
      if (state_store::writable()) {
        generate_and_save_unique_id();
      } else if (http::unique_id.empty()) {
        // Nothing can be saved this run: keep one ID for the whole run.
        http::uuid = uuid_util::uuid_t::generate();
        http::unique_id = http::uuid.string();
      }
    }
  }

  std::size_t remove_clients_with_cert(const std::string &cert) {
    auto certificate = crypto::x509(cert);
    if (!certificate) {
      return 0;
    }

    return std::erase_if(client_root.named_devices, [&certificate](const p_named_cert_t &existing) {
      auto existing_certificate = crypto::x509(existing->cert);
      return existing_certificate && X509_cmp(existing_certificate.get(), certificate.get()) == 0;
    });
  }

  void add_authorized_client(const p_named_cert_t& named_cert_p) {
    client_t &client = client_root;
    client.named_devices.push_back(named_cert_p);

#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1
    system_tray::update_tray_paired(named_cert_p->name);
#endif

    if (!config::sunshine.flags[config::flag::FRESH_STATE]) {
      save_state();
      load_state();
    }
  }

  std::shared_ptr<rtsp_stream::launch_session_t> make_launch_session(bool host_audio, bool input_only, const args_t &args, const crypto::named_cert_t* named_cert_p) {
    auto launch_session = std::make_shared<rtsp_stream::launch_session_t>();

    launch_session->id = ++session_id_counter;

    // If launched from client
    if (named_cert_p->uuid != http::unique_id) {
      auto rikey = util::from_hex_vec(get_arg(args, "rikey"), true);
      std::copy(rikey.cbegin(), rikey.cend(), std::back_inserter(launch_session->gcm_key));

      launch_session->host_audio = host_audio;

      // Encrypted RTSP is enabled with client reported corever >= 1
      auto corever = util::from_view(get_arg(args, "corever", "0"));
      if (corever >= 1) {
        launch_session->rtsp_cipher = crypto::cipher::gcm_t {
          launch_session->gcm_key, false
        };
        launch_session->rtsp_iv_counter = 0;
      }
      launch_session->rtsp_url_scheme = launch_session->rtsp_cipher ? "rtspenc://"s : "rtsp://"s;

      // Generate the unique identifiers for this connection that we will send later during RTSP handshake
      unsigned char raw_payload[8];
      RAND_bytes(raw_payload, sizeof(raw_payload));
      launch_session->av_ping_payload = util::hex_vec(raw_payload);
      RAND_bytes((unsigned char *) &launch_session->control_connect_data, sizeof(launch_session->control_connect_data));

      launch_session->iv.resize(16);
      uint32_t prepend_iv = util::endian::big<uint32_t>(util::from_view(get_arg(args, "rikeyid")));
      auto prepend_iv_p = (uint8_t *) &prepend_iv;
      std::copy(prepend_iv_p, prepend_iv_p + sizeof(prepend_iv), std::begin(launch_session->iv));
    }

    std::stringstream mode;
    if (named_cert_p->display_mode.empty()) {
      auto mode_str = get_arg(args, "mode", config::video.fallback_mode.c_str());
      mode = std::stringstream(mode_str);
      BOOST_LOG(info) << "Display mode for client ["sv << named_cert_p->name <<"] requested to ["sv << mode_str << ']';
    } else {
      mode = std::stringstream(named_cert_p->display_mode);
      BOOST_LOG(info) << "Display mode for client ["sv << named_cert_p->name <<"] overriden to ["sv << named_cert_p->display_mode << ']';
    }

    // Split mode by the char "x", to populate width/height/fps
    int x = 0;
    std::string segment;
    while (std::getline(mode, segment, 'x')) {
      if (x == 0) {
        launch_session->width = atoi(segment.c_str());
      }
      if (x == 1) {
        launch_session->height = atoi(segment.c_str());
      }
      if (x == 2) {
        auto fps = atof(segment.c_str());
        if (fps < 1000) {
          fps *= 1000;
        };
        launch_session->fps = (int)fps;
        break;
      }
      x++;
    }

    // Parsing have failed or missing components
    if (x != 2) {
      launch_session->width = 1920;
      launch_session->height = 1080;
      launch_session->fps = 60000; // 60fps * 1000 denominator
    }

    launch_session->device_name = named_cert_p->name.empty() ? "NorishineDisplay"s : named_cert_p->name;
    launch_session->unique_id = named_cert_p->uuid;
    launch_session->perm = named_cert_p->perm;
    launch_session->enable_sops = util::from_view(get_arg(args, "sops", "0"));
    launch_session->surround_info = util::from_view(get_arg(args, "surroundAudioInfo", "196610"));
    launch_session->surround_params = (get_arg(args, "surroundParams", ""));
    launch_session->gcmap = util::from_view(get_arg(args, "gcmap", "0"));
    launch_session->enable_hdr = util::from_view(get_arg(args, "hdrMode", "0"));
    launch_session->virtual_display = util::from_view(get_arg(args, "virtualDisplay", "0")) || named_cert_p->always_use_virtual_display;
    launch_session->scale_factor = util::from_view(get_arg(args, "scaleFactor", "100"));
    launch_session->tuning = config::tuning_from_view(get_arg(args, "norishineTuning", ""));

    launch_session->client_do_cmds = named_cert_p->do_cmds;
    launch_session->client_undo_cmds = named_cert_p->undo_cmds;

    launch_session->input_only = input_only;

    return launch_session;
  }

  /**
   * @brief Answer the client request that is waiting for a PIN.
   * @return `true` if a waiting request was answered.
   */
  bool write_pairing_response(pair_session_t &sess, const pt::ptree &tree) {
    std::ostringstream data;
    pt::write_xml(data, tree);

    auto &response = sess.async_insert_pin.response;
    if (response.has_left() && response.left()) {
      response.left()->close_connection_after_response = true;
      response.left()->write(data.str());
    } else if (response.has_right() && response.right()) {
      response.right()->close_connection_after_response = true;
      response.right()->write(data.str());
    } else {
      return false;
    }

    response = std::decay_t<decltype(response.left())>();
    return true;
  }

  /**
   * @brief Remove expired pairing sessions. The caller holds map_id_sess_mutex.
   */
  void expire_pair_sessions_unlocked(const std::chrono::steady_clock::time_point now) {
    for (auto it = map_id_sess.begin(); it != map_id_sess.end();) {
      if (it->second.async_insert_pin.expires_at > now) {
        ++it;
        continue;
      }

      pt::ptree tree;
      tree.put("root.paired", 0);
      tree.put("root.<xmlattr>.status_code", 408);
      tree.put("root.<xmlattr>.status_message", "Pairing session expired");
      write_pairing_response(it->second, tree);
      it = map_id_sess.erase(it);
    }
  }

  pair_session_insert_e insert_pair_session(pair_session_t sess, std::string &pairing_id) {
    std::scoped_lock lock(map_id_sess_mutex);
    const auto now = std::chrono::steady_clock::now();
    expire_pair_sessions_unlocked(now);
    pairing_id.clear();

    if (map_id_sess.contains(sess.client.uniqueID)) {
      return pair_session_insert_e::ALREADY_EXISTS;
    }
    if (map_id_sess.size() >= MAX_PENDING_PAIRING_SESSIONS) {
      return pair_session_insert_e::FULL;
    }

    do {
      pairing_id = util::hex_vec(crypto::rand(PAIRING_ID_SIZE / 2), true);
    } while (std::ranges::any_of(map_id_sess, [&](const auto &entry) {
      return entry.second.async_insert_pin.id == pairing_id;
    }));

    sess.async_insert_pin.id = pairing_id;
    sess.async_insert_pin.expires_at = now + PAIRING_SESSION_TIMEOUT;
    map_id_sess.emplace(sess.client.uniqueID, std::move(sess));
    return pair_session_insert_e::ADDED;
  }

  bool evict_abandoned_pair_session(const std::string &unique_id) {
    std::scoped_lock lock(map_id_sess_mutex);
    const auto sess_it = map_id_sess.find(unique_id);
    if (sess_it == map_id_sess.end() || sess_it->second.last_phase == PAIR_PHASE::NONE) {
      return false;
    }

    map_id_sess.erase(sess_it);
    return true;
  }

  bool is_valid_pairing_id(const std::string_view pairing_id) {
    return pairing_id.size() == PAIRING_ID_SIZE && std::ranges::all_of(pairing_id, [](const char c) {
             return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
           });
  }

  bool is_valid_pairing_pin(const std::string_view pin) {
    return pin.size() == 4 && std::ranges::all_of(pin, [](const char c) {
             return c >= '0' && c <= '9';
           });
  }

  bool is_valid_pairing_name(const std::string_view name) {
    return name.size() <= MAX_PAIRING_CLIENT_NAME_SIZE;
  }

  void expire_pair_sessions(const std::chrono::steady_clock::time_point now) {
    std::scoped_lock lock(map_id_sess_mutex);
    expire_pair_sessions_unlocked(now);
  }

  std::vector<pending_pairing_t> get_pending_pairings() {
    std::scoped_lock lock(map_id_sess_mutex);
    expire_pair_sessions_unlocked(std::chrono::steady_clock::now());

    std::vector<const pair_session_t *> pending_sessions;
    for (const auto &[unique_id, sess] : map_id_sess) {
      if (sess.last_phase == PAIR_PHASE::NONE) {
        pending_sessions.push_back(&sess);
      }
    }
    std::ranges::sort(pending_sessions, {}, [](const pair_session_t *sess) {
      return sess->async_insert_pin.expires_at;
    });

    std::vector<pending_pairing_t> result;
    result.reserve(pending_sessions.size());
    for (const auto *sess : pending_sessions) {
      result.push_back({
        .id = sess->async_insert_pin.id,
        .name = sess->async_insert_pin.device_name,
        .address = sess->async_insert_pin.address,
      });
    }
    return result;
  }

  bool cancel_pairing(const std::string_view pairing_id) {
    if (!is_valid_pairing_id(pairing_id)) {
      return false;
    }

    std::scoped_lock lock(map_id_sess_mutex);
    expire_pair_sessions_unlocked(std::chrono::steady_clock::now());

    const auto sess_it = std::ranges::find_if(map_id_sess, [&](const auto &entry) {
      return entry.second.last_phase == PAIR_PHASE::NONE && entry.second.async_insert_pin.id == pairing_id;
    });
    if (sess_it == map_id_sess.end()) {
      return false;
    }

    pt::ptree tree;
    tree.put("root.paired", 0);
    tree.put("root.<xmlattr>.status_code", 400);
    tree.put("root.<xmlattr>.status_message", "Pairing request cancelled by operator");
    write_pairing_response(sess_it->second, tree);
    map_id_sess.erase(sess_it);
    return true;
  }

  void remove_session(const pair_session_t &sess) {
    std::scoped_lock lock(map_id_sess_mutex);
    map_id_sess.erase(sess.client.uniqueID);
  }

  void fail_pair(pair_session_t &sess, pt::ptree &tree, const std::string status_msg) {
    tree.put("root.paired", 0);
    tree.put("root.<xmlattr>.status_code", 400);
    tree.put("root.<xmlattr>.status_message", status_msg);
    // Security measure: the caller removes a failed session, forcing a re-pair.
    // Removing it here would destroy `sess` while the caller still uses it.
    sess.failed = true;
    BOOST_LOG(warning) << "Pair attempt failed due to " << status_msg;
  }

  void getservercert(pair_session_t &sess, pt::ptree &tree, const std::string &pin) {
    if (sess.last_phase != PAIR_PHASE::NONE) {
      fail_pair(sess, tree, "Out of order call to getservercert");
      return;
    }
    sess.last_phase = PAIR_PHASE::GETSERVERCERT;

    if (sess.async_insert_pin.salt.size() < 32) {
      fail_pair(sess, tree, "Salt too short");
      return;
    }

    std::string_view salt_view {sess.async_insert_pin.salt.data(), 32};

    auto salt = util::from_hex<std::array<uint8_t, 16>>(salt_view, true);

    auto key = crypto::gen_aes_key(salt, pin);
    sess.cipher_key = std::make_unique<crypto::aes_t>(key);

    tree.put("root.paired", 1);
    tree.put("root.plaincert", util::hex_vec(conf_intern.servercert, true));
    tree.put("root.<xmlattr>.status_code", 200);
  }

  void clientchallenge(pair_session_t &sess, pt::ptree &tree, const std::string &challenge) {
    if (sess.last_phase != PAIR_PHASE::GETSERVERCERT) {
      fail_pair(sess, tree, "Out of order call to clientchallenge");
      return;
    }
    sess.last_phase = PAIR_PHASE::CLIENTCHALLENGE;

    if (!sess.cipher_key) {
      fail_pair(sess, tree, "Cipher key not set");
      return;
    }
    crypto::cipher::ecb_t cipher(*sess.cipher_key, false);

    std::vector<uint8_t> decrypted;
    cipher.decrypt(challenge, decrypted);

    auto x509 = crypto::x509(conf_intern.servercert);
    auto sign = crypto::signature(x509);
    auto serversecret = crypto::rand(16);

    decrypted.insert(std::end(decrypted), std::begin(sign), std::end(sign));
    decrypted.insert(std::end(decrypted), std::begin(serversecret), std::end(serversecret));

    auto hash = crypto::hash({(char *) decrypted.data(), decrypted.size()});
    auto serverchallenge = crypto::rand(16);

    std::string plaintext;
    plaintext.reserve(hash.size() + serverchallenge.size());

    plaintext.insert(std::end(plaintext), std::begin(hash), std::end(hash));
    plaintext.insert(std::end(plaintext), std::begin(serverchallenge), std::end(serverchallenge));

    std::vector<uint8_t> encrypted;
    cipher.encrypt(plaintext, encrypted);

    sess.serversecret = std::move(serversecret);
    sess.serverchallenge = std::move(serverchallenge);

    tree.put("root.paired", 1);
    tree.put("root.challengeresponse", util::hex_vec(encrypted, true));
    tree.put("root.<xmlattr>.status_code", 200);
  }

  void serverchallengeresp(pair_session_t &sess, pt::ptree &tree, const std::string &encrypted_response) {
    if (sess.last_phase != PAIR_PHASE::CLIENTCHALLENGE) {
      fail_pair(sess, tree, "Out of order call to serverchallengeresp");
      return;
    }
    sess.last_phase = PAIR_PHASE::SERVERCHALLENGERESP;

    if (!sess.cipher_key || sess.serversecret.empty()) {
      fail_pair(sess, tree, "Cipher key or serversecret not set");
      return;
    }

    std::vector<uint8_t> decrypted;
    crypto::cipher::ecb_t cipher(*sess.cipher_key, false);

    cipher.decrypt(encrypted_response, decrypted);

    sess.clienthash = std::move(decrypted);

    auto serversecret = sess.serversecret;
    auto sign = crypto::sign256(crypto::pkey(conf_intern.pkey), serversecret);

    serversecret.insert(std::end(serversecret), std::begin(sign), std::end(sign));

    tree.put("root.pairingsecret", util::hex_vec(serversecret, true));
    tree.put("root.paired", 1);
    tree.put("root.<xmlattr>.status_code", 200);
  }

  void clientpairingsecret(pair_session_t &sess, pt::ptree &tree, const std::string &client_pairing_secret) {
    if (sess.last_phase != PAIR_PHASE::SERVERCHALLENGERESP) {
      fail_pair(sess, tree, "Out of order call to clientpairingsecret");
      return;
    }
    sess.last_phase = PAIR_PHASE::CLIENTPAIRINGSECRET;

    auto &client = sess.client;

    if (client_pairing_secret.size() <= 16) {
      fail_pair(sess, tree, "Client pairing secret too short");
      return;
    }

    std::string_view secret {client_pairing_secret.data(), 16};
    std::string_view sign {client_pairing_secret.data() + secret.size(), client_pairing_secret.size() - secret.size()};

    auto x509 = crypto::x509(client.cert);
    if (!x509) {
      fail_pair(sess, tree, "Invalid client certificate");
      return;
    }
    auto x509_sign = crypto::signature(x509);

    std::string data;
    data.reserve(sess.serverchallenge.size() + x509_sign.size() + secret.size());

    data.insert(std::end(data), std::begin(sess.serverchallenge), std::end(sess.serverchallenge));
    data.insert(std::end(data), std::begin(x509_sign), std::end(x509_sign));
    data.insert(std::end(data), std::begin(secret), std::end(secret));

    auto hash = crypto::hash(data);

    // if hash not correct, probably MITM
    bool same_hash = hash.size() == sess.clienthash.size() && std::equal(hash.begin(), hash.end(), sess.clienthash.begin());
    auto verify = crypto::verify256(crypto::x509(client.cert), secret, sign);
    if (same_hash && verify) {
      tree.put("root.paired", 1);

      auto named_cert_p = std::make_shared<crypto::named_cert_t>();
      named_cert_p->name = client.name;
      for (char& c : named_cert_p->name) {
        if (c == '(') c = '[';
        else if (c == ')') c = ']';
      }
      named_cert_p->cert = std::move(client.cert);
      named_cert_p->uuid = uuid_util::uuid_t::generate().string();

      // A device that pairs again replaces its old record (Sunshine 0daeb0e3). Before,
      // save_state() kept only the first record per certificate, so the new pairing (its
      // name and default permissions) was silently dropped and the old record stayed.
      remove_clients_with_cert(named_cert_p->cert);

      // The first paired device may stream and send input; anything more is granted in
      // the web UI.
      if (client_root.named_devices.empty()) {
        named_cert_p->perm = PERM::_first_device;
      } else {
        named_cert_p->perm = PERM::_default;
      }

      named_cert_p->enable_legacy_ordering = true;
      named_cert_p->allow_client_commands = true;
      named_cert_p->always_use_virtual_display = false;

      // The caller removes the finished session from map_id_sess.
      add_authorized_client(named_cert_p);
    } else {
      tree.put("root.paired", 0);
      BOOST_LOG(warning) << "Pair attempt failed due to same_hash: " << same_hash << ", verify: " << verify;
    }

    tree.put("root.<xmlattr>.status_code", 200);
  }

  template<class T>
  struct tunnel;

  template<>
  struct tunnel<SunshineHTTPS> {
    static auto constexpr to_string = "HTTPS"sv;
  };

  template<>
  struct tunnel<SimpleWeb::HTTP> {
    static auto constexpr to_string = "NONE"sv;
  };

  inline crypto::named_cert_t* get_verified_cert(req_https_t request) {
    return (crypto::named_cert_t*)request->userp.get();
  }

  template <class T>
  void print_req(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
    BOOST_LOG(debug) << "TUNNEL :: "sv << tunnel<T>::to_string;

    BOOST_LOG(debug) << "METHOD :: "sv << request->method;
    BOOST_LOG(debug) << "DESTINATION :: "sv << request->path;

    for (auto &[name, val] : request->header) {
      BOOST_LOG(debug) << name << " -- " << http::redact_if(http::is_sensitive_header(name), val);
    }

    BOOST_LOG(debug) << " [--] "sv;

    for (auto &[name, val] : request->parse_query_string()) {
      BOOST_LOG(debug) << name << " -- " << http::redact_if(http::is_sensitive_query_arg(name), val);
    }

    BOOST_LOG(debug) << " [--] "sv;
  }

  template<class T>
  void not_found(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
    print_req<T>(request);

    pt::ptree tree;
    tree.put("root.<xmlattr>.status_code", 404);

    std::ostringstream data;

    pt::write_xml(data, tree);
    response->write(SimpleWeb::StatusCode::client_error_not_found, data.str());
    response->close_connection_after_response = true;
  }

  template <class T>
  void pair(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
    print_req<T>(request);

    pt::ptree tree;

    auto fg = util::fail_guard([&]() {
      std::ostringstream data;

      pt::write_xml(data, tree);
      response->write(data.str());
      response->close_connection_after_response = true;
    });

    if (!config::sunshine.enable_pairing) {
      tree.put("root.<xmlattr>.status_code", 403);
      tree.put("root.<xmlattr>.status_message", "Pairing is disabled for this instance");

      return;
    }

    auto args = request->parse_query_string();
    if (args.find("uniqueid"s) == std::end(args)) {
      tree.put("root.<xmlattr>.status_code", 400);
      tree.put("root.<xmlattr>.status_message", "Missing uniqueid parameter");

      return;
    }

    auto uniqID {get_arg(args, "uniqueid")};

    args_t::const_iterator it;
    if (it = args.find("phrase"); it != std::end(args)) {
      if (it->second == "getservercert"sv) {
        auto deviceName { get_arg(args, "devicename") };

        if (deviceName == "roth"sv) {
          deviceName = "Legacy Moonlight Client";
        }
        if (deviceName.size() > MAX_PAIRING_CLIENT_NAME_SIZE) {
          deviceName.resize(MAX_PAIRING_CLIENT_NAME_SIZE);
        }

        const auto otp_it = args.find("otpauth");
        const bool use_otp = otp_it != std::end(args);
        const bool pin_stdin = config::sunshine.flags[config::flag::PIN_STDIN];

        auto make_session = [&]() {
          pair_session_t sess;
          sess.client.uniqueID = uniqID;
          sess.client.name = deviceName;
          sess.client.cert = util::from_hex_vec(get_arg(args, "clientcert"), true);
          sess.async_insert_pin.salt = get_arg(args, "salt");
          sess.async_insert_pin.device_name = deviceName;
          sess.async_insert_pin.address = net::addr_to_normalized_string(request->remote_endpoint().address());
          if (!use_otp && !pin_stdin) {
            // Answered once the PIN is entered in the web UI.
            sess.async_insert_pin.response = response;
          }
          return sess;
        };

        // Held until this request is settled, so a session that is answered right away
        // (OTP) is never listed as waiting for a PIN.
        std::unique_lock lock(map_id_sess_mutex);

        std::string pairing_id;
        auto insert_result = insert_pair_session(make_session(), pairing_id);
        if (insert_result == pair_session_insert_e::ALREADY_EXISTS && evict_abandoned_pair_session(uniqID)) {
          // The client gave up on a handshake that got past the PIN step; start over.
          insert_result = insert_pair_session(make_session(), pairing_id);
        }
        switch (insert_result) {
          case pair_session_insert_e::ALREADY_EXISTS:
            tree.put("root.paired", 0);
            tree.put("root.<xmlattr>.status_code", 409);
            tree.put("root.<xmlattr>.status_message", "A pairing session with this uniqueid already exists");
            return;
          case pair_session_insert_e::FULL:
            tree.put("root.paired", 0);
            tree.put("root.<xmlattr>.status_code", 503);
            tree.put("root.<xmlattr>.status_message", "Too many pending pairing sessions");
            return;
          case pair_session_insert_e::ADDED:
            break;
        }

        auto &new_sess = map_id_sess.at(uniqID);
        BOOST_LOG(debug) << new_sess.client.cert;

        if (use_otp) {
          if (one_time_pin.empty() || (std::chrono::steady_clock::now() - otp_creation_time > OTP_EXPIRE_DURATION)) {
            one_time_pin.clear();
            otp_passphrase.clear();
            otp_device_name.clear();
            map_id_sess.erase(uniqID);
            tree.put("root.<xmlattr>.status_code", 503);
            tree.put("root.<xmlattr>.status_message", "OTP auth not available.");
            return;
          }

          auto hash = util::hex(crypto::hash(one_time_pin + new_sess.async_insert_pin.salt + otp_passphrase), true);
          if (hash.to_string_view() == otp_it->second) {
            if (!otp_device_name.empty()) {
              new_sess.client.name = std::move(otp_device_name);
            }

            getservercert(new_sess, tree, one_time_pin);

            one_time_pin.clear();
            otp_passphrase.clear();
            otp_device_name.clear();
          } else {
            // Always return positive, attackers will fail in the next steps.
            getservercert(new_sess, tree, crypto::rand(16));
          }

          if (new_sess.failed) {
            map_id_sess.erase(uniqID);
          }
          return;
        }

        if (pin_stdin) {
          lock.unlock();

          std::string pin;
          std::cout << "Please insert pin: "sv;
          std::getline(std::cin, pin);

          lock.lock();
          expire_pair_sessions_unlocked(std::chrono::steady_clock::now());
          const auto sess_it = map_id_sess.find(uniqID);
          if (sess_it == map_id_sess.end() || sess_it->second.async_insert_pin.id != pairing_id) {
            tree.put("root.paired", 0);
            tree.put("root.<xmlattr>.status_code", 408);
            tree.put("root.<xmlattr>.status_message", "Pairing session expired");
            return;
          }

          getservercert(sess_it->second, tree, pin);
          if (sess_it->second.failed) {
            map_id_sess.erase(sess_it);
          }
          return;
        }

#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1
        system_tray::update_tray_require_pin();
#endif
        fg.disable();
        return;
      } else if (it->second == "pairchallenge"sv) {
        tree.put("root.paired", 1);
        tree.put("root.<xmlattr>.status_code", 200);
        return;
      }
    }

    std::scoped_lock lock(map_id_sess_mutex);
    expire_pair_sessions_unlocked(std::chrono::steady_clock::now());
    auto sess_it = map_id_sess.find(uniqID);
    if (sess_it == std::end(map_id_sess)) {
      tree.put("root.<xmlattr>.status_code", 400);
      tree.put("root.<xmlattr>.status_message", "Invalid uniqueid");

      return;
    }

    bool pairing_complete = false;
    if (it = args.find("clientchallenge"); it != std::end(args)) {
      auto challenge = util::from_hex_vec(it->second, true);
      clientchallenge(sess_it->second, tree, challenge);
    } else if (it = args.find("serverchallengeresp"); it != std::end(args)) {
      auto encrypted_response = util::from_hex_vec(it->second, true);
      serverchallengeresp(sess_it->second, tree, encrypted_response);
    } else if (it = args.find("clientpairingsecret"); it != std::end(args)) {
      auto pairingsecret = util::from_hex_vec(it->second, true);
      clientpairingsecret(sess_it->second, tree, pairingsecret);
      pairing_complete = true;
    } else {
      fail_pair(sess_it->second, tree, "Invalid pairing request");
    }

    if (pairing_complete || sess_it->second.failed) {
      map_id_sess.erase(sess_it);
    }
  }

  bool pin(const std::string_view pairing_id, std::string pin, std::string name) {
    if (!is_valid_pairing_id(pairing_id) || !is_valid_pairing_pin(pin) || !is_valid_pairing_name(name)) {
      return false;
    }

    std::scoped_lock lock(map_id_sess_mutex);
    expire_pair_sessions_unlocked(std::chrono::steady_clock::now());
    // Only the request the user picked, never just the first one (GHSA-36ff-frg7-492f).
    const auto sess_it = std::ranges::find_if(map_id_sess, [&](const auto &entry) {
      return entry.second.last_phase == PAIR_PHASE::NONE && entry.second.async_insert_pin.id == pairing_id;
    });
    if (sess_it == map_id_sess.end()) {
      return false;
    }

    auto &sess = sess_it->second;
    pt::ptree tree;
    getservercert(sess, tree, pin);
    if (!sess.failed && !name.empty()) {
      sess.client.name = std::move(name);
    }

    const bool response_written = write_pairing_response(sess, tree);
    const bool success = response_written && !sess.failed;
    if (!success) {
      map_id_sess.erase(sess_it);
    }
    return success;
  }

  template<class T>
  void serverinfo(std::shared_ptr<typename SimpleWeb::ServerBase<T>::Response> response, std::shared_ptr<typename SimpleWeb::ServerBase<T>::Request> request) {
    print_req<T>(request);

    int pair_status = 0;
    if constexpr (std::is_same_v<SunshineHTTPS, T>) {
      auto args = request->parse_query_string();
      auto clientID = args.find("uniqueid"s);

      if (clientID != std::end(args)) {
        pair_status = 1;
      }
    }

    auto local_endpoint = request->local_endpoint();

    pt::ptree tree;

    tree.put("root.<xmlattr>.status_code", 200);
    tree.put("root.hostname", config::nvhttp.sunshine_name);

    tree.put("root.appversion", VERSION);
    tree.put("root.GfeVersion", GFE_VERSION);
    tree.put("root.uniqueid", http::unique_id);
    tree.put("root.HttpsPort", net::map_port(PORT_HTTPS));
    tree.put("root.ExternalPort", net::map_port(PORT_HTTP));
    tree.put("root.MaxLumaPixelsHEVC", video::active_hevc_mode > 1 ? "1869449984" : "0");

    // Only include the MAC address for requests sent from paired clients over HTTPS.
    // For HTTP requests, use a placeholder MAC address that Moonlight knows to ignore.
    if constexpr (std::is_same_v<SunshineHTTPS, T>) {
      tree.put("root.mac", platf::get_mac_address(net::addr_to_normalized_string(local_endpoint.address())));

      auto named_cert_p = get_verified_cert(request);
      if (!!(named_cert_p->perm & PERM::server_cmd)) {
        pt::ptree& root_node = tree.get_child("root");

        if (config::sunshine.server_cmds.size() > 0) {
          // Broadcast server_cmds
          for (const auto& cmd : config::sunshine.server_cmds) {
            pt::ptree cmd_node;
            cmd_node.put_value(cmd.cmd_name);
            root_node.push_back(std::make_pair("ServerCommand", cmd_node));
          }
        }
      } else {
        BOOST_LOG(debug) << "Permission Get ServerCommand denied for [" << named_cert_p->name << "] (" << (uint32_t)named_cert_p->perm << ")";
      }

      tree.put("root.Permission", std::to_string((uint32_t)named_cert_p->perm));

    #ifdef _WIN32
      tree.put("root.VirtualDisplayCapable", true);
      if (!!(named_cert_p->perm & PERM::_all_actions)) {
        tree.put("root.VirtualDisplayDriverReady", proc::vDisplayDriverStatus == VDISPLAY::DRIVER_STATUS::OK);
      } else {
        tree.put("root.VirtualDisplayDriverReady", true);
      }
    #endif
    } else {
      tree.put("root.mac", "00:00:00:00:00:00");
      tree.put("root.Permission", "0");
    }

    // Moonlight clients track LAN IPv6 addresses separately from LocalIP which is expected to
    // always be an IPv4 address. If we return that same IPv6 address here, it will clobber the
    // stored LAN IPv4 address. To avoid this, we need to return an IPv4 address in this field
    // when we get a request over IPv6.
    //
    // HACK: We should return the IPv4 address of local interface here, but we don't currently
    // have that implemented. For now, we will emulate the behavior of GFE+GS-IPv6-Forwarder,
    // which returns 127.0.0.1 as LocalIP for IPv6 connections. Moonlight clients with IPv6
    // support know to ignore this bogus address.
    if (local_endpoint.address().is_v6() && !local_endpoint.address().to_v6().is_v4_mapped()) {
      tree.put("root.LocalIP", "127.0.0.1");
    } else {
      tree.put("root.LocalIP", net::addr_to_normalized_string(local_endpoint.address()));
    }

    uint32_t codec_mode_flags = SCM_H264;
    if (video::last_encoder_probe_supported_yuv444_for_codec[0]) {
      codec_mode_flags |= SCM_H264_HIGH8_444;
    }
    if (video::active_hevc_mode >= 2) {
      codec_mode_flags |= SCM_HEVC;
      if (video::last_encoder_probe_supported_yuv444_for_codec[1]) {
        codec_mode_flags |= SCM_HEVC_REXT8_444;
      }
    }
    if (video::active_hevc_mode >= 3) {
      codec_mode_flags |= SCM_HEVC_MAIN10;
      if (video::last_encoder_probe_supported_yuv444_for_codec[1]) {
        codec_mode_flags |= SCM_HEVC_REXT10_444;
      }
    }
    if (video::active_av1_mode >= 2) {
      codec_mode_flags |= SCM_AV1_MAIN8;
      if (video::last_encoder_probe_supported_yuv444_for_codec[2]) {
        codec_mode_flags |= SCM_AV1_HIGH8_444;
      }
    }
    if (video::active_av1_mode >= 3) {
      codec_mode_flags |= SCM_AV1_MAIN10;
      if (video::last_encoder_probe_supported_yuv444_for_codec[2]) {
        codec_mode_flags |= SCM_AV1_HIGH10_444;
      }
    }
    tree.put("root.ServerCodecModeSupport", codec_mode_flags);

    if (!config::nvhttp.external_ip.empty()) {
      tree.put("root.ExternalIP", config::nvhttp.external_ip);
    }

    tree.put("root.PairStatus", pair_status);

    if constexpr (std::is_same_v<SunshineHTTPS, T>) {
      int current_appid = proc::proc.running();
      // When input only mode is enabled, the only resume method should be launching the same app again.
      if (config::input.enable_input_only_mode && current_appid != proc::input_only_app_id) {
        current_appid = 0;
      }
      tree.put("root.currentgame", current_appid);
      tree.put("root.currentgameuuid", proc::proc.get_running_app_uuid());
      tree.put("root.state", current_appid > 0 ? "SUNSHINE_SERVER_BUSY" : "SUNSHINE_SERVER_FREE");
    } else {
      tree.put("root.currentgame", 0);
      tree.put("root.currentgameuuid", "");
      tree.put("root.state", "SUNSHINE_SERVER_FREE");
    }

    // Norishine management API (roadmap D4): one optional element that Moonlight and
    // Artemis ignore. What a device may do is in root.Permission (manage_read, manage_write).
    tree.put("root.Norishine.ApiVersion", API_VERSION);
    tree.put("root.Norishine.Features", std::string {config::video.client_tuning ? "tuning,"sv : ""sv} + "status,stats,session,adaptive,recommendations,update,config.schema,config.read,config.write,restart,"s + std::string {manage_api::features});

    std::ostringstream data;

    pt::write_xml(data, tree);
    response->write(data.str());
    response->close_connection_after_response = true;
  }

  /**
   * @brief Management API for paired clients (/api/v1 on the HTTPS port).
   *
   * Clients authenticate with the certificate they paired with, as for every HTTPS
   * request; each call also needs a management permission, which is never granted by
   * default (crypto::PERM::manage_read, crypto::PERM::manage_write).
   */
  namespace api {
    void send_json(resp_https_t response, SimpleWeb::StatusCode code, const nlohmann::json &body) {
      SimpleWeb::CaseInsensitiveMultimap headers;
      headers.emplace("Content-Type", "application/json");
      response->write(code, body.dump(), headers);
      response->close_connection_after_response = true;
    }

    /**
     * @brief Check a permission; answers 403 when the device lacks it.
     * @param any Any one of these permission bits is enough.
     */
    bool allowed(resp_https_t response, req_https_t request, PERM any, std::string_view what) {
      auto named_cert_p = get_verified_cert(request);
      if (!!(named_cert_p->perm & any)) {
        return true;
      }
      BOOST_LOG(info) << "Management API: "sv << what << " denied for ["sv << named_cert_p->name << "] ("sv << (uint32_t) named_cert_p->perm << ')';
      send_json(response, SimpleWeb::StatusCode::client_error_forbidden, {
        {"status", false},
        {"error", "This device may not " + std::string {what} + ". Grant it in the host's web UI, under paired devices."},
      });
      return false;
    }

    // Changing settings includes reading them.
    constexpr auto read_perm = PERM::_all_manage;

    void status(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      if (!allowed(response, request, read_perm, "read host status")) {
        return;
      }

      auto named_cert_p = get_verified_cert(request);
      auto tree = host_status();
      tree["device"] = {{"name", named_cert_p->name}, {"permissions", (uint32_t) named_cert_p->perm}};
      send_json(response, SimpleWeb::StatusCode::success_ok, tree);
    }

    void stats(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      if (!allowed(response, request, read_perm, "read host status")) {
        return;
      }
      send_json(response, SimpleWeb::StatusCode::success_ok, host_stats());
    }

    SimpleWeb::StatusCode recommend_status(int code) {
      switch (code) {
        case 200:
          return SimpleWeb::StatusCode::success_ok;
        case 400:
          return SimpleWeb::StatusCode::client_error_bad_request;
        case 409:
          return SimpleWeb::StatusCode::client_error_conflict;
        default:
          return SimpleWeb::StatusCode::server_error_internal_server_error;
      }
    }

    void recommendations_get(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      if (!allowed(response, request, read_perm, "read host settings")) {
        return;
      }
      auto encoder = recommend::encoder_for(video::active_encoder_name());
      send_json(response, SimpleWeb::StatusCode::success_ok, recommend::store().describe(encoder, config::sunshine.config_file));
    }

    /**
     * @brief POST /api/v1/recommendations/apply and /rollback: change settings like PATCH /api/v1/config.
     */
    void recommendations_change(resp_https_t response, req_https_t request, bool apply) {
      print_req<SunshineHTTPS>(request);
      if (!allowed(response, request, PERM::manage_write, "change host settings")) {
        return;
      }
      nlohmann::json body;
      if (!request->content.string().empty()) {
        body = nlohmann::json::parse(request->content.string(), nullptr, false);
        if (body.is_discarded()) {
          send_json(response, SimpleWeb::StatusCode::client_error_bad_request, {{"status", false}, {"error", "Invalid JSON"}});
          return;
        }
      }
      auto encoder = recommend::encoder_for(video::active_encoder_name());
      auto reply = apply ? recommend::store().apply(body, encoder, config::sunshine.config_file) : recommend::store().rollback(config::sunshine.config_file);
      if (reply.code == 200) {
        BOOST_LOG(info) << "Management API: ["sv << get_verified_cert(request)->name << "] "sv << (apply ? "applied"sv : "undid"sv) << " recommended settings"sv;
      }
      send_json(response, recommend_status(reply.code), reply.body);
    }

    void config_schema(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      if (!allowed(response, request, read_perm, "read host settings")) {
        return;
      }
      auto query = request->parse_query_string();
      auto locale = query.find("locale");
      auto body = config::schema::describe(locale == query.end() ? std::string {} : locale->second);
      body["status"] = true;
      send_json(response, SimpleWeb::StatusCode::success_ok, body);
    }

    void config_get(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      if (!allowed(response, request, read_perm, "read host settings")) {
        return;
      }
      auto body = config::schema::read_config_file(config::sunshine.config_file);
      body["status"] = true;
      send_json(response, SimpleWeb::StatusCode::success_ok, body);
    }

    void config_patch(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      if (!allowed(response, request, PERM::manage_write, "change host settings")) {
        return;
      }

      auto content_type = request->header.find("content-type");
      if (content_type == request->header.end() || content_type->second.find("application/json") == std::string::npos) {
        send_json(response, SimpleWeb::StatusCode::client_error_unsupported_media_type, {{"status", false}, {"error", "Content-Type must be application/json"}});
        return;
      }

      auto input = nlohmann::json::parse(request->content.string(), nullptr, false);
      if (input.is_discarded()) {
        send_json(response, SimpleWeb::StatusCode::client_error_bad_request, {{"status", false}, {"error", "Invalid JSON"}});
        return;
      }

      auto change = config::schema::validate_change(input);
      if (!change.errors.empty()) {
        send_json(response, SimpleWeb::StatusCode::client_error_bad_request, {{"status", false}, {"errors", change.errors}});
        return;
      }

      auto result = config::schema::apply_change(change, config::sunshine.config_file);
      if (!result.written) {
        send_json(response, SimpleWeb::StatusCode::server_error_internal_server_error, {{"status", false}, {"error", "Couldn't write the config file"}});
        return;
      }

      BOOST_LOG(info) << "Management API: ["sv << get_verified_cert(request)->name << "] changed settings: "sv << result.changed.dump();
      send_json(response, SimpleWeb::StatusCode::success_ok, {{"status", true}, {"changed", result.changed}, {"restart_required", result.restart_required}});
    }

    void restart(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      if (!allowed(response, request, PERM::manage_write, "restart the host")) {
        return;
      }

      BOOST_LOG(info) << "Management API: ["sv << get_verified_cert(request)->name << "] restarts the host"sv;
      send_json(response, SimpleWeb::StatusCode::success_ok, {{"status", true}});

      // Answer first: platf::restart() may not return.
      std::thread([]() {
        std::this_thread::sleep_for(500ms);
        proc::proc.terminate();
        platf::restart();
      }).detach();
    }

    void send_reply(resp_https_t response, const manage_api::reply_t &reply) {
      send_json(response, reply.code, reply.body);
    }

    /**
     * @brief Read a JSON request body; answers 400 or 415 itself when it can't.
     */
    bool json_body(resp_https_t response, req_https_t request, nlohmann::json &out) {
      auto content_type = request->header.find("content-type");
      if (content_type == request->header.end() || content_type->second.find("application/json") == std::string::npos) {
        send_json(response, SimpleWeb::StatusCode::client_error_unsupported_media_type, {{"status", false}, {"error", "Content-Type must be application/json"}});
        return false;
      }
      manage_api::reply_t error;
      if (!manage_api::parse_body(request->content.string(), out, error)) {
        send_reply(response, error);
        return false;
      }
      return true;
    }

    std::string path_id(req_https_t request) {
      return request->path_match.size() > 1 ? request->path_match[1].str() : std::string {};
    }

    void update_status(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      if (allowed(response, request, read_perm, "read host status")) {
        auto query = request->parse_query_string();
        send_json(response, SimpleWeb::StatusCode::success_ok, updater::status(query.find("refresh") != query.end()));
      }
    }

    void update_start(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      nlohmann::json body;
      if (allowed(response, request, PERM::manage_write, "update the host") && json_body(response, request, body)) {
        send_reply(response, updater::start(body));
      }
    }

    /**
     * @brief The requesting device's own stream, as GET /api/v1/session answers it.
     */
    nlohmann::json own_session(stream::session_t &session) {
      static const char *codecs[] = {"H.264", "HEVC", "AV1"};
      auto info = stream::session::describe(session);
      auto entry = stream_metrics::to_json(info.metrics);
      entry["width"] = info.width;
      entry["height"] = info.height;
      entry["target_fps"] = info.fps;
      entry["start_bitrate_kbps"] = info.bitrate_kbps;
      entry["codec"] = info.codec >= 0 && info.codec < 3 ? codecs[info.codec] : "unknown";
      entry["hdr"] = info.hdr;
      entry["tuning"] = std::string {config::to_string(info.tuning)};
      entry["adaptive"] = info.adaptive ? adaptive::to_json(*info.adaptive) : nlohmann::json(nullptr);
      return {{"status", true}, {"stream", entry}};
    }

    /**
     * @brief Find the requesting device's stream; answers 404 when it has none.
     */
    std::shared_ptr<stream::session_t> own_stream(resp_https_t response, req_https_t request) {
      auto named_cert_p = get_verified_cert(request);
      auto session = rtsp_stream::find_session(named_cert_p->uuid);
      if (!session) {
        send_json(response, SimpleWeb::StatusCode::client_error_not_found, {{"status", false}, {"error", "This device has no stream running"}});
      }
      return session;
    }

    /**
     * @brief GET /api/v1/session: the requesting device's own stream. No management
     *        permission is needed: a device only sees its own stream.
     */
    void session_get(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      if (auto session = own_stream(response, request)) {
        send_json(response, SimpleWeb::StatusCode::success_ok, own_session(*session));
      }
    }

    /**
     * @brief POST /api/v1/session/report: the client's own measurements for its stream's
     *        controller (adaptive.h). Answers as GET /api/v1/session.
     */
    void session_report(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      nlohmann::json body;
      if (!json_body(response, request, body)) {
        return;
      }
      adaptive::client_report_t report;
      if (auto error = adaptive::parse_report(body, report); !error.empty()) {
        send_json(response, SimpleWeb::StatusCode::client_error_bad_request, {{"status", false}, {"error", error}});
        return;
      }
      if (auto session = own_stream(response, request)) {
        stream::session::report(*session, report);
        send_json(response, SimpleWeb::StatusCode::success_ok, own_session(*session));
      }
    }

    void apps_list(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      if (allowed(response, request, read_perm, "read the app list")) {
        send_reply(response, manage_api::list_apps());
      }
    }

    void apps_create(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      nlohmann::json body;
      if (allowed(response, request, PERM::manage_write, "change apps") && json_body(response, request, body)) {
        send_reply(response, manage_api::create_app(body));
      }
    }

    void apps_replace(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      nlohmann::json body;
      if (allowed(response, request, PERM::manage_write, "change apps") && json_body(response, request, body)) {
        send_reply(response, manage_api::replace_app(path_id(request), body));
      }
    }

    void apps_delete(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      if (allowed(response, request, PERM::manage_write, "change apps")) {
        send_reply(response, manage_api::delete_app(path_id(request)));
      }
    }

    void apps_order(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      nlohmann::json body;
      if (allowed(response, request, PERM::manage_write, "change apps") && json_body(response, request, body)) {
        send_reply(response, manage_api::order_apps(body));
      }
    }

    void apps_launch(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      if (allowed(response, request, PERM::manage_write, "start apps on the host")) {
        BOOST_LOG(info) << "Management API: ["sv << get_verified_cert(request)->name << "] starts an app"sv;
        send_reply(response, manage_api::launch_app(path_id(request)));
      }
    }

    void apps_close(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      if (allowed(response, request, PERM::manage_write, "close the running app")) {
        BOOST_LOG(info) << "Management API: ["sv << get_verified_cert(request)->name << "] closes the running app"sv;
        send_reply(response, manage_api::close_app());
      }
    }

    void devices_list(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      if (allowed(response, request, read_perm, "read the paired devices")) {
        send_reply(response, manage_api::list_devices(get_verified_cert(request)->uuid));
      }
    }

    void devices_patch(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      nlohmann::json body;
      if (allowed(response, request, PERM::manage_write, "change paired devices") && json_body(response, request, body)) {
        auto self = get_verified_cert(request);
        auto reply = manage_api::patch_device(path_id(request), body, self->uuid);
        if (reply.code == SimpleWeb::StatusCode::success_ok) {
          BOOST_LOG(info) << "Management API: ["sv << self->name << "] changed device ["sv << reply.body["device"].value("name", ""s) << ']';
        }
        send_reply(response, reply);
      }
    }

    void devices_unpair(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      if (allowed(response, request, PERM::manage_write, "remove paired devices")) {
        BOOST_LOG(info) << "Management API: ["sv << get_verified_cert(request)->name << "] removes a paired device"sv;
        send_reply(response, manage_api::unpair_device(path_id(request)));
      }
    }

    void devices_disconnect(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      if (allowed(response, request, PERM::manage_write, "end streams")) {
        BOOST_LOG(info) << "Management API: ["sv << get_verified_cert(request)->name << "] ends a stream"sv;
        send_reply(response, manage_api::disconnect_device(path_id(request)));
      }
    }

    void displays_list(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      if (allowed(response, request, read_perm, "read the displays")) {
        send_reply(response, manage_api::list_displays());
      }
    }

    void displays_reset(resp_https_t response, req_https_t request) {
      print_req<SunshineHTTPS>(request);
      if (allowed(response, request, PERM::manage_write, "reset display changes")) {
        send_reply(response, manage_api::reset_displays());
      }
    }
  }  // namespace api

  nlohmann::json host_status() {
    nlohmann::json app = nullptr;
    if (int id = proc::proc.running(); id > 0) {
      auto uuid = proc::proc.get_running_app_uuid();
      std::string name;
      for (const auto &ctx : proc::proc.get_apps()) {
        if (ctx.uuid == uuid) {
          name = ctx.name;
        }
      }
      app = {{"id", id}, {"uuid", uuid}, {"name", name}};
    }

    static const char *codecs[] = {"H.264", "HEVC", "AV1"};
    static const char *states[] = {"stopped", "stopping", "starting", "running"};
    nlohmann::json sessions = nlohmann::json::array();
    for (const auto &s : rtsp_stream::get_all_session_info()) {
      sessions.push_back({
        {"uuid", s.uuid},
        {"name", s.name},
        {"address", s.address},
        {"width", s.width},
        {"height", s.height},
        {"fps", s.fps},
        {"bitrate_kbps", s.bitrate_kbps},
        {"codec", s.codec >= 0 && s.codec < 3 ? codecs[s.codec] : "unknown"},
        {"hdr", s.hdr},
        {"state", states[(int) s.state & 3]},
      });
    }

    return {
      {"status", true},
      {"api_version", API_VERSION},
      {"hostname", config::nvhttp.sunshine_name},
      {"version", version::project},
      {"platform", SUNSHINE_PLATFORM},
      {"uniqueid", http::unique_id},
      {"running_app", app},
      {"active_sessions", rtsp_stream::session_count()},
      {"sessions", sessions},
      {"encoder", std::string {video::active_encoder_name()}},
      {"port", net::map_port(PORT_HTTP)},
      {"port_moved_for", config::sunshine.port_moved_for},
    };
  }

  nlohmann::json host_stats() {
    nlohmann::json sessions = nlohmann::json::array();
    for (const auto &s : rtsp_stream::get_all_session_info()) {
      auto entry = stream_metrics::to_json(s.metrics);
      entry["uuid"] = s.uuid;
      entry["name"] = s.name;
      entry["adaptive"] = s.adaptive ? adaptive::to_json(*s.adaptive) : nlohmann::json(nullptr);
      sessions.push_back(std::move(entry));
    }
    return {
      {"status", true},
      {"host", host_stats::to_json(host_stats::current())},
      {"encoder", std::string {video::active_encoder_name()}},
      {"sessions", sessions},
    };
  }

  nlohmann::json get_all_clients() {
    nlohmann::json named_cert_nodes = nlohmann::json::array();
    client_t &client = client_root;
    std::list<std::string> connected_uuids = rtsp_stream::get_all_session_uuids();

    for (auto &named_cert : client.named_devices) {
      nlohmann::json named_cert_node;
      named_cert_node["name"] = named_cert->name;
      named_cert_node["uuid"] = named_cert->uuid;
      named_cert_node["display_mode"] = named_cert->display_mode;
      named_cert_node["perm"] = static_cast<uint32_t>(named_cert->perm);
      named_cert_node["enable_legacy_ordering"] = named_cert->enable_legacy_ordering;
      named_cert_node["allow_client_commands"] = named_cert->allow_client_commands;
      named_cert_node["always_use_virtual_display"] = named_cert->always_use_virtual_display;

      // Add "do" commands if available
      if (!named_cert->do_cmds.empty()) {
        nlohmann::json do_cmds_node = nlohmann::json::array();
        for (const auto &cmd : named_cert->do_cmds) {
          do_cmds_node.push_back(crypto::command_entry_t::serialize(cmd));
        }
        named_cert_node["do"] = do_cmds_node;
      }

      // Add "undo" commands if available
      if (!named_cert->undo_cmds.empty()) {
        nlohmann::json undo_cmds_node = nlohmann::json::array();
        for (const auto &cmd : named_cert->undo_cmds) {
          undo_cmds_node.push_back(crypto::command_entry_t::serialize(cmd));
        }
        named_cert_node["undo"] = undo_cmds_node;
      }

      // Determine connection status
      bool connected = false;
      if (connected_uuids.empty()) {
        connected = false;
      } else {
        for (auto it = connected_uuids.begin(); it != connected_uuids.end(); ++it) {
          if (*it == named_cert->uuid) {
            connected = true;
            connected_uuids.erase(it);
            break;
          }
        }
      }
      named_cert_node["connected"] = connected;

      named_cert_nodes.push_back(named_cert_node);
    }

    return named_cert_nodes;
  }

  void applist(resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);

    pt::ptree tree;

    auto g = util::fail_guard([&]() {
      std::ostringstream data;

      pt::write_xml(data, tree);
      response->write(data.str());
      response->close_connection_after_response = true;
    });

    auto &apps = tree.add_child("root", pt::ptree {});

    apps.put("<xmlattr>.status_code", 200);

    auto named_cert_p = get_verified_cert(request);
    if (!!(named_cert_p->perm & PERM::_all_actions)) {
      auto current_appid = proc::proc.running();
      auto should_hide_inactive_apps = config::input.enable_input_only_mode && current_appid > 0 && current_appid != proc::input_only_app_id;

      auto app_list = proc::proc.get_apps();

      bool enable_legacy_ordering = config::sunshine.legacy_ordering && named_cert_p->enable_legacy_ordering;
      size_t bits;
      if (enable_legacy_ordering) {
        bits = zwpad::pad_width_for_count(app_list.size());
      }

      for (size_t i = 0; i < app_list.size(); i++) {
        auto& app = app_list[i];
        auto appid = util::from_view(app.id);
        if (should_hide_inactive_apps) {
          if (
            appid != current_appid
            && appid != proc::input_only_app_id
            && appid != proc::terminate_app_id
          ) {
            continue;
          }
        } else {
          if (appid == proc::terminate_app_id) {
            continue;
          }
        }

        std::string app_name;
        if (enable_legacy_ordering) {
          app_name = zwpad::pad_for_ordering(app.name, bits, i);
        } else {
          app_name = app.name;
        }

        pt::ptree app_node;

        app_node.put("IsHdrSupported"s, video::active_hevc_mode == 3 ? 1 : 0);
        app_node.put("AppTitle"s, app_name);
        app_node.put("UUID", app.uuid);
        app_node.put("IDX", app.idx);
        app_node.put("ID", app.id);

        apps.push_back(std::make_pair("App", std::move(app_node)));
      }
    } else {
      BOOST_LOG(debug) << "Permission ListApp denied for [" << named_cert_p->name << "] (" << (uint32_t)named_cert_p->perm << ")";

      pt::ptree app_node;

      app_node.put("IsHdrSupported"s, 0);
      app_node.put("AppTitle"s, "Permission Denied");
      app_node.put("UUID", "");
      app_node.put("IDX", "0");
      app_node.put("ID", "114514");

      apps.push_back(std::make_pair("App", std::move(app_node)));

      return;
    }

  }

  void launch(bool &host_audio, resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);

    pt::ptree tree;
    auto g = util::fail_guard([&]() {
      std::ostringstream data;

      if (tree.empty()) {
        BOOST_LOG(error) << EMPTY_PROPERTY_TREE_ERROR_MSG;
      }

      pt::write_xml(data, tree);
      response->write(data.str());
      response->close_connection_after_response = true;
    });

    auto args = request->parse_query_string();

    auto appid_str = get_arg(args, "appid", "0");
    auto appuuid_str = get_arg(args, "appuuid", "");
    auto appid = util::from_view(appid_str);
    auto current_appid = proc::proc.running();
    auto current_app_uuid = proc::proc.get_running_app_uuid();
    bool is_input_only = config::input.enable_input_only_mode && (appid == proc::input_only_app_id || (appuuid_str == REMOTE_INPUT_UUID));

    auto named_cert_p = get_verified_cert(request);
    auto perm = PERM::launch;

    BOOST_LOG(verbose) << "Launching app [" << appid_str << "] with UUID [" << appuuid_str << "]";
    // BOOST_LOG(verbose) << "QS: " << request->query_string;

    // If we have already launched an app, we should allow clients with view permission to join the input only or current app's session.
    if (
      current_appid > 0
      && (appuuid_str != TERMINATE_APP_UUID || appid != proc::terminate_app_id)
      && (is_input_only || appid == current_appid || (!appuuid_str.empty() && appuuid_str == current_app_uuid))
    ) {
      perm = PERM::_allow_view;
    }

    if (!(named_cert_p->perm & perm)) {
      BOOST_LOG(info) << "Permission LaunchApp denied for [" << named_cert_p->name << "] (" << (uint32_t)named_cert_p->perm << "): change it on the web UI's Devices page";

      tree.put("root.resume", 0);
      tree.put("root.<xmlattr>.status_code", 403);
      tree.put("root.<xmlattr>.status_message", "Permission denied. Change this device's permissions on the host's Devices page.");

      return;
    }
    if (
      args.find("rikey"s) == std::end(args) ||
      args.find("rikeyid"s) == std::end(args) ||
      args.find("localAudioPlayMode"s) == std::end(args) ||
      (args.find("appid"s) == std::end(args) && args.find("appuuid"s) == std::end(args))
    ) {
      tree.put("root.resume", 0);
      tree.put("root.<xmlattr>.status_code", 400);
      tree.put("root.<xmlattr>.status_message", "Missing a required launch parameter");

      return;
    }

    if (!is_input_only) {
      // Special handling for the "terminate" app
      if (
        (config::input.enable_input_only_mode && appid == proc::terminate_app_id)
        || appuuid_str == TERMINATE_APP_UUID
      ) {
        proc::proc.terminate();

        tree.put("root.resume", 0);
        tree.put("root.<xmlattr>.status_code", 410);
        tree.put("root.<xmlattr>.status_message", "App terminated.");

        return;
      }

      if (
        current_appid > 0
        && current_appid != proc::input_only_app_id
        && (
          (appid > 0 && appid != current_appid)
          || (!appuuid_str.empty() && appuuid_str != current_app_uuid)
        )
      ) {
        tree.put("root.resume", 0);
        tree.put("root.<xmlattr>.status_code", 400);
        tree.put("root.<xmlattr>.status_message", "An app is already running on this host");

        return;
      }
    }

    host_audio = util::from_view(get_arg(args, "localAudioPlayMode"));
    auto launch_session = make_launch_session(host_audio, is_input_only, args, named_cert_p);

    auto encryption_mode = net::encryption_mode_for_address(request->remote_endpoint().address());
    if (!launch_session->rtsp_cipher && encryption_mode == config::ENCRYPTION_MODE_MANDATORY) {
      BOOST_LOG(error) << "Rejecting client that cannot comply with mandatory encryption requirement"sv;

      tree.put("root.<xmlattr>.status_code", 403);
      tree.put("root.<xmlattr>.status_message", "Encryption is mandatory for this host but unsupported by the client");
      tree.put("root.gamesession", 0);

      return;
    }

    bool no_active_sessions = rtsp_stream::session_count() == 0;

    if (is_input_only) {
      BOOST_LOG(info) << "Launching input only session..."sv;

      launch_session->client_do_cmds.clear();
      launch_session->client_undo_cmds.clear();

      // Still probe encoders once, if input only session is launched first
      // But we're ignoring if it's successful or not
      if (no_active_sessions && !proc::proc.virtual_display) {
        video::probe_encoders();
        if (current_appid == 0) {
          proc::proc.launch_input_only();
        }
      }
    } else if (appid > 0 || !appuuid_str.empty()) {
      if (appid == current_appid || (!appuuid_str.empty() && appuuid_str == current_app_uuid)) {
        // We're basically resuming the same app

        BOOST_LOG(debug) << "Resuming app [" << proc::proc.get_last_run_app_name() << "] from launch app path...";

        if (!proc::proc.allow_client_commands || !named_cert_p->allow_client_commands) {
          launch_session->client_do_cmds.clear();
          launch_session->client_undo_cmds.clear();
        }

        if (current_appid == proc::input_only_app_id) {
          launch_session->input_only = true;
        }

        if (no_active_sessions && !proc::proc.virtual_display) {
          display_device::configure_display(config::video, *launch_session);
          if (video::probe_encoders()) {
            tree.put("root.resume", 0);
            tree.put("root.<xmlattr>.status_code", 503);
            tree.put("root.<xmlattr>.status_message", "Failed to initialize video capture/encoding. Is a display connected and turned on?");

            return;
          }
        }
      } else {
        const auto& apps = proc::proc.get_apps();
        auto app_iter = std::find_if(apps.begin(), apps.end(), [&appid_str, &appuuid_str](const auto _app) {
          return _app.id == appid_str || _app.uuid == appuuid_str;
        });

        if (app_iter == apps.end()) {
          BOOST_LOG(error) << "Couldn't find app with ID ["sv << appid_str << "] or UUID ["sv << appuuid_str << ']';
          tree.put("root.<xmlattr>.status_code", 404);
          tree.put("root.<xmlattr>.status_message", "Cannot find requested application");
          tree.put("root.gamesession", 0);
          return;
        }

        if (!app_iter->allow_client_commands) {
          launch_session->client_do_cmds.clear();
          launch_session->client_undo_cmds.clear();
        }

        auto err = proc::proc.execute(*app_iter, launch_session);
        if (err) {
          tree.put("root.<xmlattr>.status_code", err);
          tree.put(
            "root.<xmlattr>.status_message",
            err == 503
            ? "Failed to initialize video capture/encoding. Is a display connected and turned on?"
            : "Failed to start the specified application");
          tree.put("root.gamesession", 0);

          return;
        }
      }
    } else {
      tree.put("root.<xmlattr>.status_code", 403);
      tree.put("root.<xmlattr>.status_message", "How did you get here?");
      tree.put("root.gamesession", 0);
    }

    tree.put("root.<xmlattr>.status_code", 200);
    tree.put(
      "root.sessionUrl0",
      std::format(
        "{}{}:{}",
        launch_session->rtsp_url_scheme,
        net::addr_to_url_escaped_string(request->local_endpoint().address()),
        static_cast<int>(net::map_port(rtsp_stream::RTSP_SETUP_PORT))
      )
    );
    tree.put("root.gamesession", 1);

    rtsp_stream::launch_session_raise(launch_session);
  }

  void resume(bool &host_audio, resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);

    pt::ptree tree;
    auto g = util::fail_guard([&]() {
      std::ostringstream data;

      if (tree.empty()) {
        BOOST_LOG(error) << EMPTY_PROPERTY_TREE_ERROR_MSG;
      }

      pt::write_xml(data, tree);
      response->write(data.str());
      response->close_connection_after_response = true;
    });

    auto named_cert_p = get_verified_cert(request);
    if (!(named_cert_p->perm & PERM::_allow_view)) {
      BOOST_LOG(info) << "Permission ViewApp denied for [" << named_cert_p->name << "] (" << (uint32_t)named_cert_p->perm << "): change it on the web UI's Devices page";

      tree.put("root.resume", 0);
      tree.put("root.<xmlattr>.status_code", 403);
      tree.put("root.<xmlattr>.status_message", "Permission denied. Change this device's permissions on the host's Devices page.");

      return;
    }

    auto current_appid = proc::proc.running();
    if (current_appid == 0) {
      tree.put("root.resume", 0);
      tree.put("root.<xmlattr>.status_code", 503);
      tree.put("root.<xmlattr>.status_message", "No running app to resume");

      return;
    }

    auto args = request->parse_query_string();
    if (
      args.find("rikey"s) == std::end(args) ||
      args.find("rikeyid"s) == std::end(args)
    ) {
      tree.put("root.resume", 0);
      tree.put("root.<xmlattr>.status_code", 400);
      tree.put("root.<xmlattr>.status_message", "Missing a required resume parameter");

      return;
    }

    // Newer Moonlight clients send localAudioPlayMode on /resume too,
    // so we should use it if it's present in the args and there are
    // no active sessions we could be interfering with.
    const bool no_active_sessions {rtsp_stream::session_count() == 0};
    if (no_active_sessions && args.find("localAudioPlayMode"s) != std::end(args)) {
      host_audio = util::from_view(get_arg(args, "localAudioPlayMode"));
    }
    auto launch_session = make_launch_session(host_audio, false, args, named_cert_p);

    if (!proc::proc.allow_client_commands || !named_cert_p->allow_client_commands) {
      launch_session->client_do_cmds.clear();
      launch_session->client_undo_cmds.clear();
    }

    if (config::input.enable_input_only_mode && current_appid == proc::input_only_app_id) {
      launch_session->input_only = true;
    }

    if (no_active_sessions && !proc::proc.virtual_display) {
      // We want to prepare display only if there are no active sessions
      // and the current session isn't virtual display at the moment.
      // This should be done before probing encoders as it could change the active displays.
      display_device::configure_display(config::video, *launch_session);

      // Probe encoders again before streaming to ensure our chosen
      // encoder matches the active GPU (which could have changed
      // due to hotplugging, driver crash, primary monitor change,
      // or any number of other factors).
      if (video::probe_encoders()) {
        tree.put("root.resume", 0);
        tree.put("root.<xmlattr>.status_code", 503);
        tree.put("root.<xmlattr>.status_message", "Failed to initialize video capture/encoding. Is a display connected and turned on?");

        return;
      }
    }

    auto encryption_mode = net::encryption_mode_for_address(request->remote_endpoint().address());
    if (!launch_session->rtsp_cipher && encryption_mode == config::ENCRYPTION_MODE_MANDATORY) {
      BOOST_LOG(error) << "Rejecting client that cannot comply with mandatory encryption requirement"sv;

      tree.put("root.<xmlattr>.status_code", 403);
      tree.put("root.<xmlattr>.status_message", "Encryption is mandatory for this host but unsupported by the client");
      tree.put("root.gamesession", 0);

      return;
    }

    tree.put("root.<xmlattr>.status_code", 200);
    tree.put(
      "root.sessionUrl0",
      std::format(
        "{}{}:{}",
        launch_session->rtsp_url_scheme,
        net::addr_to_url_escaped_string(request->local_endpoint().address()),
        static_cast<int>(net::map_port(rtsp_stream::RTSP_SETUP_PORT))
      )
    );
    tree.put("root.resume", 1);

    rtsp_stream::launch_session_raise(launch_session);

#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1
    system_tray::update_tray_client_connected(named_cert_p->name);
#endif
  }

  void cancel(resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);

    pt::ptree tree;
    auto g = util::fail_guard([&]() {
      std::ostringstream data;

      pt::write_xml(data, tree);
      response->write(data.str());
      response->close_connection_after_response = true;
    });

    auto named_cert_p = get_verified_cert(request);
    if (!(named_cert_p->perm & PERM::launch)) {
      BOOST_LOG(debug) << "Permission CancelApp denied for [" << named_cert_p->name << "] (" << (uint32_t)named_cert_p->perm << ")";

      tree.put("root.resume", 0);
      tree.put("root.<xmlattr>.status_code", 403);
      tree.put("root.<xmlattr>.status_message", "Permission denied");

      return;
    }

    tree.put("root.cancel", 1);
    tree.put("root.<xmlattr>.status_code", 200);

    rtsp_stream::terminate_sessions();

    if (proc::proc.running() > 0) {
      proc::proc.terminate();
    }

    // The config needs to be reverted regardless of whether "proc::proc.terminate()" was called or not.
    display_device::revert_configuration();
  }

  void appasset(resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);

    auto fg = util::fail_guard([&]() {
      response->write(SimpleWeb::StatusCode::server_error_internal_server_error);
      response->close_connection_after_response = true;
    });

    auto named_cert_p = get_verified_cert(request);

    if (!(named_cert_p->perm & PERM::_all_actions)) {
      BOOST_LOG(debug) << "Permission Get AppAsset denied for [" << named_cert_p->name << "] (" << (uint32_t)named_cert_p->perm << ")";

      fg.disable();
      response->write(SimpleWeb::StatusCode::client_error_unauthorized);
      response->close_connection_after_response = true;
      return;
    }

    auto args = request->parse_query_string();
    auto app_image = proc::proc.get_app_image(util::from_view(get_arg(args, "appid")));

    fg.disable();

    std::ifstream in(app_image, std::ios::binary);
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "image/png");
    response->write(SimpleWeb::StatusCode::success_ok, in, headers);
    response->close_connection_after_response = true;
  }

  void getClipboard(resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);

    auto named_cert_p = get_verified_cert(request);

    if (
      !(named_cert_p->perm & PERM::_allow_view)
      || !(named_cert_p->perm & PERM::clipboard_read)
    ) {
      BOOST_LOG(debug) << "Permission Read Clipboard denied for [" << named_cert_p->name << "] (" << (uint32_t)named_cert_p->perm << ")";

      response->write(SimpleWeb::StatusCode::client_error_unauthorized);
      response->close_connection_after_response = true;
      return;
    }

    auto args = request->parse_query_string();
    auto clipboard_type = get_arg(args, "type");
    if (clipboard_type != "text"sv) {
      BOOST_LOG(debug) << "Clipboard type [" << clipboard_type << "] is not supported!";

      response->write(SimpleWeb::StatusCode::client_error_bad_request);
      response->close_connection_after_response = true;
      return;
    }

    std::list<std::string> connected_uuids = rtsp_stream::get_all_session_uuids();

    bool found = !connected_uuids.empty();

    if (found) {
      found = (std::find(connected_uuids.begin(), connected_uuids.end(), named_cert_p->uuid) != connected_uuids.end());
    }

    if (!found) {
      BOOST_LOG(debug) << "Client ["<< named_cert_p->name << "] trying to get clipboard is not connected to a stream";

      response->write(SimpleWeb::StatusCode::client_error_forbidden);
      response->close_connection_after_response = true;
      return;
    }

    std::string content = platf::get_clipboard();
    response->write(content);
    return;
  }

  void
  setClipboard(resp_https_t response, req_https_t request) {
    print_req<SunshineHTTPS>(request);

    auto named_cert_p = get_verified_cert(request);

    if (
      !(named_cert_p->perm & PERM::_allow_view)
      || !(named_cert_p->perm & PERM::clipboard_set)
    ) {
      BOOST_LOG(debug) << "Permission Write Clipboard denied for [" << named_cert_p->name << "] (" << (uint32_t)named_cert_p->perm << ")";

      response->write(SimpleWeb::StatusCode::client_error_unauthorized);
      response->close_connection_after_response = true;
      return;
    }

    auto args = request->parse_query_string();
    auto clipboard_type = get_arg(args, "type");
    if (clipboard_type != "text"sv) {
      BOOST_LOG(debug) << "Clipboard type [" << clipboard_type << "] is not supported!";

      response->write(SimpleWeb::StatusCode::client_error_bad_request);
      response->close_connection_after_response = true;
      return;
    }

    std::list<std::string> connected_uuids = rtsp_stream::get_all_session_uuids();

    bool found = !connected_uuids.empty();

    if (found) {
      found = (std::find(connected_uuids.begin(), connected_uuids.end(), named_cert_p->uuid) != connected_uuids.end());
    }

    if (!found) {
      BOOST_LOG(debug) << "Client ["<< named_cert_p->name << "] trying to set clipboard is not connected to a stream";

      response->write(SimpleWeb::StatusCode::client_error_forbidden);
      response->close_connection_after_response = true;
      return;
    }

    std::string content = request->content.string();

    bool success = platf::set_clipboard(content);

    if (!success) {
      BOOST_LOG(debug) << "Setting clipboard failed!";

      response->write(SimpleWeb::StatusCode::server_error_internal_server_error);
      response->close_connection_after_response = true;
    }

    response->write();
    return;
  }

  void setup(const std::string &pkey, const std::string &cert) {
    conf_intern.pkey = pkey;
    conf_intern.servercert = cert;
  }

  void start() {
    auto shutdown_event = mail::man->event<bool>(mail::shutdown);

    auto port_http = net::map_port(PORT_HTTP);
    auto port_https = net::map_port(PORT_HTTPS);
    auto address_family = net::af_from_enum_string(config::sunshine.address_family);

    bool clean_slate = config::sunshine.flags[config::flag::FRESH_STATE];

    if (!clean_slate) {
      load_state();
    }

    auto pkey = file_handler::read_file(config::nvhttp.pkey.c_str());
    auto cert = file_handler::read_file(config::nvhttp.cert.c_str());
    setup(pkey, cert);

    // resume doesn't always get the parameter "localAudioPlayMode"
    // launch will store it in host_audio
    bool host_audio {};

    https_server_t https_server {config::nvhttp.cert, config::nvhttp.pkey};
    http_server_t http_server;

    // Verify certificates after establishing connection
    https_server.verify = [](req_https_t req, SSL *ssl) {
      crypto::x509_t x509 {
#if OPENSSL_VERSION_MAJOR >= 3
        SSL_get1_peer_certificate(ssl)
#else
        SSL_get_peer_certificate(ssl)
#endif
      };
      if (!x509) {
        BOOST_LOG(info) << "unknown -- denied"sv;
        return false;
      }

      bool verified = false;
      p_named_cert_t named_cert_p;

      auto fg = util::fail_guard([&]() {
        char subject_name[256];

        X509_NAME_oneline(X509_get_subject_name(x509.get()), subject_name, sizeof(subject_name));

        if (verified) {
          BOOST_LOG(debug) << subject_name << " -- "sv << "verified, device name: "sv << named_cert_p->name;
        } else {
          BOOST_LOG(debug) << subject_name << " -- "sv << "denied"sv;
        }

      });

      auto err_str = cert_chain.verify(x509.get(), named_cert_p);
      if (err_str) {
        BOOST_LOG(warning) << "SSL Verification error :: "sv << err_str;
        return verified;
      }

      verified = true;
      req->userp = named_cert_p;

      return true;
    };

    https_server.on_verify_failed = [](resp_https_t resp, req_https_t req) {
      pt::ptree tree;
      auto g = util::fail_guard([&]() {
        std::ostringstream data;

        pt::write_xml(data, tree);
        resp->write(data.str());
        resp->close_connection_after_response = true;
      });

      tree.put("root.<xmlattr>.status_code"s, 401);
      tree.put("root.<xmlattr>.query"s, req->path);
      tree.put("root.<xmlattr>.status_message"s, "The client is not authorized. Certificate verification failed."s);
    };

    https_server.default_resource["GET"] = not_found<SunshineHTTPS>;
    https_server.resource["^/serverinfo$"]["GET"] = serverinfo<SunshineHTTPS>;
    https_server.resource["^/pair$"]["GET"] = pair<SunshineHTTPS>;
    https_server.resource["^/applist$"]["GET"] = applist;
    https_server.resource["^/appasset$"]["GET"] = appasset;
    https_server.resource["^/launch$"]["GET"] = [&host_audio](auto resp, auto req) {
      launch(host_audio, resp, req);
    };
    https_server.resource["^/resume$"]["GET"] = [&host_audio](auto resp, auto req) {
      resume(host_audio, resp, req);
    };
    https_server.resource["^/cancel$"]["GET"] = cancel;
    https_server.resource["^/actions/clipboard$"]["GET"] = getClipboard;
    https_server.resource["^/actions/clipboard$"]["POST"] = setClipboard;
    https_server.resource["^/api/v1/status$"]["GET"] = api::status;
    https_server.resource["^/api/v1/stats$"]["GET"] = api::stats;
    https_server.resource["^/api/v1/session$"]["GET"] = api::session_get;
    https_server.resource["^/api/v1/session/report$"]["POST"] = api::session_report;
    https_server.resource["^/api/v1/update$"]["GET"] = api::update_status;
    https_server.resource["^/api/v1/update$"]["POST"] = api::update_start;
    https_server.resource["^/api/v1/recommendations$"]["GET"] = api::recommendations_get;
    https_server.resource["^/api/v1/recommendations/apply$"]["POST"] = [](auto resp, auto req) {
      api::recommendations_change(resp, req, true);
    };
    https_server.resource["^/api/v1/recommendations/rollback$"]["POST"] = [](auto resp, auto req) {
      api::recommendations_change(resp, req, false);
    };
    https_server.resource["^/api/v1/config/schema$"]["GET"] = api::config_schema;
    https_server.resource["^/api/v1/config$"]["GET"] = api::config_get;
    https_server.resource["^/api/v1/config$"]["PATCH"] = api::config_patch;
    https_server.resource["^/api/v1/restart$"]["POST"] = api::restart;
    https_server.resource["^/api/v1/apps$"]["GET"] = api::apps_list;
    https_server.resource["^/api/v1/apps$"]["POST"] = api::apps_create;
    https_server.resource["^/api/v1/apps/order$"]["POST"] = api::apps_order;
    https_server.resource["^/api/v1/apps/close$"]["POST"] = api::apps_close;
    https_server.resource["^/api/v1/apps/([^/]+)$"]["PUT"] = api::apps_replace;
    https_server.resource["^/api/v1/apps/([^/]+)$"]["DELETE"] = api::apps_delete;
    https_server.resource["^/api/v1/apps/([^/]+)/launch$"]["POST"] = api::apps_launch;
    https_server.resource["^/api/v1/devices$"]["GET"] = api::devices_list;
    https_server.resource["^/api/v1/devices/([^/]+)$"]["PATCH"] = api::devices_patch;
    https_server.resource["^/api/v1/devices/([^/]+)$"]["DELETE"] = api::devices_unpair;
    https_server.resource["^/api/v1/devices/([^/]+)/disconnect$"]["POST"] = api::devices_disconnect;
    https_server.resource["^/api/v1/displays$"]["GET"] = api::displays_list;
    https_server.resource["^/api/v1/displays/reset$"]["POST"] = api::displays_reset;

    https_server.config.reuse_address = true;
    https_server.config.address = net::af_to_any_address_string(address_family);
    https_server.config.port = port_https;

    http_server.default_resource["GET"] = not_found<SimpleWeb::HTTP>;
    http_server.resource["^/serverinfo$"]["GET"] = serverinfo<SimpleWeb::HTTP>;
    http_server.resource["^/pair$"]["GET"] = pair<SimpleWeb::HTTP>;

    http_server.config.reuse_address = true;
    http_server.config.address = net::af_to_any_address_string(address_family);
    http_server.config.port = port_http;

    auto accept_and_run = [&](auto *http_server) {
      try {
        http_server->start();
      } catch (boost::system::system_error &err) {
        // It's possible the exception gets thrown after calling http_server->stop() from a different thread
        if (shutdown_event->peek()) {
          return;
        }

        BOOST_LOG(fatal) << "Couldn't start http server on ports ["sv << port_https << ", "sv << port_https << "]: "sv << err.what();
        shutdown_event->raise(true);
        return;
      }
    };
    std::thread ssl {accept_and_run, &https_server};
    std::thread tcp {accept_and_run, &http_server};

    // Wait for any event
    shutdown_event->view();

    {
      std::scoped_lock lock(map_id_sess_mutex);
      map_id_sess.clear();
    }

    https_server.stop();
    http_server.stop();

    ssl.join();
    tcp.join();
  }

  std::string request_otp(const std::string& passphrase, const std::string& deviceName) {
    if (passphrase.size() < 4) {
      return "";
    }

    std::scoped_lock lock(map_id_sess_mutex);
    one_time_pin = crypto::rand_alphabet(4, "0123456789"sv);
    otp_passphrase = passphrase;
    otp_device_name = deviceName;
    otp_creation_time = std::chrono::steady_clock::now();

    return one_time_pin;
  }

  void
  erase_all_clients() {
    client_t client;
    client_root = client;
    cert_chain.clear();
    save_state();
    load_state();
  }

  void stop_session(stream::session_t& session, bool graceful) {
    if (graceful) {
      stream::session::graceful_stop(session);
    } else {
      stream::session::stop(session);
    }
  }

  bool find_and_stop_session(const std::string& uuid, bool graceful) {
    auto session = rtsp_stream::find_session(uuid);
    if (session) {
      stop_session(*session, graceful);
      return true;
    }
    return false;
  }

  void update_session_info(stream::session_t& session, const std::string& name, const crypto::PERM newPerm) {
    stream::session::update_device_info(session, name, newPerm);
  }

  bool find_and_udpate_session_info(const std::string& uuid, const std::string& name, const crypto::PERM newPerm) {
    auto session = rtsp_stream::find_session(uuid);
    if (session) {
      update_session_info(*session, name, newPerm);
      return true;
    }
    return false;
  }

  bool update_device_info(
    const std::string& uuid,
    const std::string& name,
    const std::string& display_mode,
    const cmd_list_t& do_cmds,
    const cmd_list_t& undo_cmds,
    const crypto::PERM newPerm,
    const bool enable_legacy_ordering,
    const bool allow_client_commands,
    const bool always_use_virtual_display
  ) {
    find_and_udpate_session_info(uuid, name, newPerm);

    client_t &client = client_root;
    auto it = client.named_devices.begin();
    for (; it != client.named_devices.end(); ++it) {
      auto named_cert_p = *it;
      if (named_cert_p->uuid == uuid) {
        named_cert_p->name = name;
        named_cert_p->display_mode = display_mode;
        named_cert_p->perm = newPerm;
        named_cert_p->do_cmds = do_cmds;
        named_cert_p->undo_cmds = undo_cmds;
        named_cert_p->enable_legacy_ordering = enable_legacy_ordering;
        named_cert_p->allow_client_commands = allow_client_commands;
        named_cert_p->always_use_virtual_display = always_use_virtual_display;
        save_state();
        return true;
      }
    }

    return false;
  }

  bool unpair_client(const std::string_view uuid) {
    bool removed = false;
    client_t &client = client_root;
    for (auto it = client.named_devices.begin(); it != client.named_devices.end();) {
      if ((*it)->uuid == uuid) {
        it = client.named_devices.erase(it);
        removed = true;
      } else {
        ++it;
      }
    }

    save_state();
    load_state();

    if (removed) {
      auto session = rtsp_stream::find_session(uuid);
      if (session) {
        stop_session(*session, true);
      }

      if (client.named_devices.empty()) {
        proc::proc.terminate();
      }
    }

    return removed;
  }
}  // namespace nvhttp
