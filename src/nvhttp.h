/**
 * @file src/nvhttp.h
 * @brief Declarations for the nvhttp (GameStream) server.
 */
// macros
#pragma once

// standard includes
#include <chrono>
#include <cstddef>
#include <list>
#include <string>
#include <string_view>
#include <vector>

// lib includes
#include <boost/property_tree/ptree.hpp>
#include <nlohmann/json.hpp>
#include <Simple-Web-Server/server_https.hpp>

// local includes
#include "crypto.h"
#include "rtsp.h"
#include "thread_safe.h"

using namespace std::chrono_literals;

/**
 * @brief Contains all the functions and variables related to the nvhttp (GameStream) server.
 */
namespace nvhttp {

  using args_t = SimpleWeb::CaseInsensitiveMultimap;
  using cmd_list_t = std::list<crypto::command_entry_t>;

  /**
   * @brief The protocol version.
   * @details The version of the GameStream protocol we are mocking.
   * @note The negative 4th number indicates to Moonlight that this is Sunshine.
   */
  constexpr auto VERSION = "7.1.431.-1";

  /**
   * @brief The GFE version we are replicating.
   */
  constexpr auto GFE_VERSION = "3.23.0.74";

  /**
   * @brief The HTTP port, as a difference from the config port.
   */
  constexpr auto PORT_HTTP = 0;

  /**
   * @brief The HTTPS port, as a difference from the config port.
   */
  constexpr auto PORT_HTTPS = -5;

  /**
   * @brief Version of the Norishine management API on the HTTPS port (/api/v1).
   */
  constexpr int API_VERSION = 1;

  constexpr auto OTP_EXPIRE_DURATION = 180s;

  /**
   * @brief Maximum number of pairing sessions kept at one time.
   */
  constexpr std::size_t MAX_PENDING_PAIRING_SESSIONS = 32;

  /**
   * @brief Number of hexadecimal characters in a pairing request ID.
   */
  constexpr std::size_t PAIRING_ID_SIZE = 32;

  /**
   * @brief Maximum length in bytes of a paired client's name.
   */
  constexpr std::size_t MAX_PAIRING_CLIENT_NAME_SIZE = 128;

  /**
   * @brief Lifetime of an incomplete pairing session.
   */
  constexpr auto PAIRING_SESSION_TIMEOUT = std::chrono::minutes {5};

  /**
   * @brief Start the nvhttp server.
   * @examples
   * nvhttp::start();
   * @examples_end
   */
  void start();

  std::string
  get_arg(const args_t &args, const char *name, const char *default_value = nullptr);

  // Helper function to extract command entries
  cmd_list_t
  extract_command_entries(const nlohmann::json& j, const std::string& key);

  std::shared_ptr<rtsp_stream::launch_session_t>
  make_launch_session(bool host_audio, bool input_only, const args_t &args, const crypto::named_cert_t* named_cert_p);

  /**
   * @brief Setup the nvhttp server.
   * @param pkey
   * @param cert
   */
  void setup(const std::string &pkey, const std::string &cert);

  class SunshineHTTPS: public SimpleWeb::HTTPS {
  public:
    SunshineHTTPS(boost::asio::io_context &io_context, boost::asio::ssl::context &ctx):
        SimpleWeb::HTTPS(io_context, ctx) {
    }

    virtual ~SunshineHTTPS() {
      // Gracefully shutdown the TLS connection
      SimpleWeb::error_code ec;
      shutdown(ec);
    }
  };

  enum class PAIR_PHASE {
    NONE,  ///< Sunshine is not in a pairing phase
    GETSERVERCERT,  ///< Sunshine is in the get server certificate phase
    CLIENTCHALLENGE,  ///< Sunshine is in the client challenge phase
    SERVERCHALLENGERESP,  ///< Sunshine is in the server challenge response phase
    CLIENTPAIRINGSECRET  ///< Sunshine is in the client pairing secret phase
  };

  struct pair_session_t {
    struct {
      std::string uniqueID = {};
      std::string cert = {};
      std::string name = {};
    } client;

    std::unique_ptr<crypto::aes_t> cipher_key = {};
    std::vector<uint8_t> clienthash = {};

    std::string serversecret = {};
    std::string serverchallenge = {};

    struct {
      util::Either<
        std::shared_ptr<typename SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response>,
        std::shared_ptr<typename SimpleWeb::ServerBase<SunshineHTTPS>::Response>>
        response;
      std::string salt = {};
      std::string id = {};  ///< Unguessable ID the web UI uses to approve this request.
      std::string device_name = {};  ///< Device name reported by the client (untrusted).
      std::string address = {};  ///< Address the pairing request came from.
      std::chrono::steady_clock::time_point expires_at = std::chrono::steady_clock::time_point::max();  ///< Deadline for completing pairing.
    } async_insert_pin;

    /**
     * @brief used as a security measure to prevent out of order calls
     */
    PAIR_PHASE last_phase = PAIR_PHASE::NONE;
    bool failed = false;  ///< Set when a pairing step fails; the caller then removes the session.
  };

  /**
   * @brief A PIN pairing request waiting for approval in the web UI.
   */
  struct pending_pairing_t {
    std::string id;  ///< Unguessable pairing request ID.
    std::string name;  ///< Device name reported by the client (untrusted).
    std::string address;  ///< Address the request came from.
  };

  /**
   * @brief Result of adding a pairing session.
   */
  enum class pair_session_insert_e {
    ADDED,  ///< The session was added.
    ALREADY_EXISTS,  ///< A pending session with the same client unique ID exists.
    FULL  ///< MAX_PENDING_PAIRING_SESSIONS has been reached.
  };

  /**
   * @brief Check the format of a pairing request ID received from the web UI.
   * @return `true` for exactly PAIRING_ID_SIZE hexadecimal characters.
   */
  bool is_valid_pairing_id(std::string_view pairing_id);

  /**
   * @brief Check a PIN received from the web UI.
   * @return `true` for exactly four ASCII digits.
   */
  bool is_valid_pairing_pin(std::string_view pin);

  /**
   * @brief Check a client name received from the web UI.
   * @return `true` if the name fits MAX_PAIRING_CLIENT_NAME_SIZE. An empty name keeps
   * the name the device reported.
   */
  bool is_valid_pairing_name(std::string_view name);

  /**
   * @brief Add a new pairing session, giving it an unguessable ID and an expiry time.
   * @param sess Pairing session to add.
   * @param pairing_id Receives the new session's ID when it was added.
   */
  pair_session_insert_e insert_pair_session(pair_session_t sess, std::string &pairing_id);

  /**
   * @brief Remove a client's session that already got past the PIN step.
   *
   * A new `getservercert` from the same client means it gave up on that handshake, which
   * can never resume. Sessions still waiting for a PIN are kept, so a stray request
   * cannot replace a request the user is about to approve.
   * @return `true` if a session was removed.
   */
  bool evict_abandoned_pair_session(const std::string &unique_id);

  /**
   * @brief Remove pairing sessions whose deadline has passed.
   */
  void expire_pair_sessions(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

  /**
   * @brief List PIN pairing requests waiting for approval, oldest first.
   */
  std::vector<pending_pairing_t> get_pending_pairings();

  /**
   * @brief Reject a PIN pairing request that is waiting for approval.
   * @return `true` if the request was found and cancelled.
   */
  bool cancel_pairing(std::string_view pairing_id);

  /**
   * @brief removes the temporary pairing session
   * @param sess
   */
  void remove_session(const pair_session_t &sess);

  /**
   * @brief Pair, phase 1
   *
   * Moonlight will send a salt and client certificate, we'll also need the user provided pin.
   *
   * PIN and SALT will be used to derive a shared AES key that needs to be stored
   * in order to be used to decrypt_symmetric in the next phases.
   *
   * At this stage we only have to send back our public certificate.
   */
  void getservercert(pair_session_t &sess, boost::property_tree::ptree &tree, const std::string &pin);

  /**
   * @brief Pair, phase 2
   *
   * Using the AES key that we generated in phase 1 we have to decrypt the client challenge,
   *
   * We generate a SHA256 hash with the following:
   *  - Decrypted challenge
   *  - Server certificate signature
   *  - Server secret: a randomly generated secret
   *
   * The hash + server_challenge will then be AES encrypted and sent as the `challengeresponse` in the returned XML
   */
  void clientchallenge(pair_session_t &sess, boost::property_tree::ptree &tree, const std::string &challenge);

  /**
   * @brief Pair, phase 3
   *
   * Moonlight will send back a `serverchallengeresp`: an AES encrypted client hash,
   * we have to send back the `pairingsecret`:
   * using our private key we have to sign the certificate_signature + server_secret (generated in phase 2)
   */
  void serverchallengeresp(pair_session_t &sess, boost::property_tree::ptree &tree, const std::string &encrypted_response);

  /**
   * @brief Pair, phase 4 (final)
   *
   * We now have to use everything we exchanged before in order to verify and finally pair the clients
   *
   * We'll check the client_hash obtained at phase 3, it should contain the following:
   *   - The original server_challenge
   *   - The signature of the X509 client_cert
   *   - The unencrypted client_pairing_secret
   * We'll check that SHA256(server_challenge + client_public_cert_signature + client_secret) == client_hash
   *
   * Then using the client certificate public key we should be able to verify that
   * the client secret has been signed by Moonlight
   */
  void clientpairingsecret(pair_session_t &sess, boost::property_tree::ptree &tree, const std::string &client_pairing_secret);

  /**
   * @brief Apply the user supplied PIN to the selected pairing request.
   * @param pairing_id ID of the pairing request, from get_pending_pairings().
   * @param pin The user supplied pin.
   * @param name The user supplied name. Empty keeps the name the device reported.
   * @return `true` if the PIN was passed on to the waiting client. A wrong PIN is only
   * detected in a later pairing step.
   * @examples
   * bool pin_status = nvhttp::pin("0123456789abcdef0123456789abcdef", "1234", "laptop");
   * @examples_end
   */
  bool pin(std::string_view pairing_id, std::string pin, std::string name);

  std::string request_otp(const std::string& passphrase, const std::string& deviceName);

  /**
   * @brief Remove single client.
   * @param uuid The UUID of the client to remove.
   * @examples
   * nvhttp::unpair_client("4D7BB2DD-5704-A405-B41C-891A022932E1");
   * @examples_end
   */
  bool unpair_client(std::string_view uuid);

  /**
   * @brief Remove every paired client whose certificate is the given one.
   *
   * Used when a device pairs again, so it never has more than one record.
   * Does not save the state; the caller does.
   * @param cert PEM certificate.
   * @return Number of records removed.
   */
  std::size_t remove_clients_with_cert(const std::string &cert);

  /**
   * @brief Get all paired clients.
   * @return The list of all paired clients.
   * @examples
   * nlohmann::json clients = nvhttp::get_all_clients();
   * @examples_end
   */
  nlohmann::json get_all_clients();

  /**
   * @brief Host status for the management API: name, version, running app and active streams.
   */
  nlohmann::json host_status();

  /**
   * @brief Remove all paired clients.
   * @examples
   * nvhttp::erase_all_clients();
   * @examples_end
   */
  void erase_all_clients();

  /**
   * @brief      Stops a session.
   *
   * @param      session   The session
   * @param[in]  graceful  Whether to stop gracefully
   */
  void stop_session(stream::session_t& session, bool graceful);

  /**
   * @brief      Finds and stop session.
   *
   * @param[in]  uuid      The uuid string
   * @param[in]  graceful  Whether to stop gracefully
   */
  bool find_and_stop_session(const std::string& uuid, bool graceful);

  /**
   * @brief      Update device info associated to the session
   *
   * @param      session  The session
   * @param[in]  name     New name
   * @param[in]  newPerm  New permission
   */
  void update_session_info(stream::session_t& session, const std::string& name, const crypto::PERM newPerm);

  /**
   * @brief      Finds and udpate session information.
   *
   * @param[in]  uuid     The uuid string
   * @param[in]  name     New name
   * @param[in]  newPerm  New permission
   */
  bool find_and_udpate_session_info(const std::string& uuid, const std::string& name, const crypto::PERM newPerm);

  /**
   * @brief      Update device info
   *
   * @param[in]  uuid       The uuid string
   * @param[in]  name       New name
   * @param[in]  do_cmds    The do commands
   * @param[in]  undo_cmds  The undo commands
   * @param[in]  newPerm    New permission
   * @param[in]  enable_legacy_ordering  Enable legacy ordering
   * @param[in]  allow_client_commands  Allow client commands
   * @param[in]  always_use_virtual_display  Always use virtual display
   * 
   * @return     Whether the update is successful
   */
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
  );
}  // namespace nvhttp
