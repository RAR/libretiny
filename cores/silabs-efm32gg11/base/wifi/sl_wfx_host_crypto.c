/* Copyright (c) Andrew Rankin 2026-06-12. */
// WF200 Secure Link host hooks: the crypto callbacks declared in the FMAC
// driver's sl_wfx_host_api.h, implemented with mbedtls, plus the FreeRTOS task
// that runs session-key renegotiation. The algorithms are fixed by the Secure
// Link protocol the WF200 enforces:
//
//   - Key agreement: X25519. Public keys travel as 32 little-endian bytes;
//     mbedtls MPIs are big-endian, so keys are reversed at the wire boundary.
//   - Public-key authentication: HMAC-SHA512 keyed with the 32-byte MAC key.
//   - Session key: the first 16 bytes of SHA-256 over the little-endian
//     shared secret.
//   - Record cipher: AES-128-CCM, 12-byte nonce from the {hp, rx, tx} packet
//     counters, 16-byte tag, no associated data.
//
// Entropy comes from the GG11 TRNG through mbedtls_hardware_poll()
// (lt_wfx_entropy.c).

#include "sl_wfx.h"

#ifdef SL_WFX_USE_SECURE_LINK

#if !SL_WFX_SLK_CURVE25519
#error "This WF200 host port implements the Curve25519 Secure Link mode only."
#endif

#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "secure_link/sl_wfx_secure_link.h"
#include "sl_wfx_host_conf.h"

// mbedtls 3.x marks the ecp_point coordinates private; the X/Z Montgomery
// coordinates have to be read and written directly.
#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#include "mbedtls/ccm.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/ecp.h"
#include "mbedtls/entropy.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/sha256.h"

// The 32-byte MAC key provisioned into the GG11 device-information page
// (base 0x0FE08000 + 0x3D0) on parts burned for enforced Secure Link. An
// unwritten key reads as all 0xFF.
#define LT_SLK_MAC_KEY_ADDR ((const uint8_t *)(0x0FE08000UL + 0x3D0UL))

// Renegotiation runs the DRBG seeding, SHA-512 entropy accumulation and X25519
// on this stack (about 2 KiB at the deepest point); sized with margin.
#define LT_SLK_TASK_STACK_WORDS 1536
#define LT_SLK_TASK_PRIORITY	(configMAX_PRIORITIES - 3)

// Serialises use of the session key and nonce counters between the bus task
// (decode) and key agreement (verify).
static SemaphoreHandle_t s_slk_mutex;
static TaskHandle_t s_slk_task;

// Key-agreement state. The ephemeral key pair made in compute_pub_key() is
// consumed by verify_pub_key(); free_crypto_context() releases everything.
static mbedtls_entropy_context s_entropy;
static mbedtls_ctr_drbg_context s_drbg;
static mbedtls_ecp_group s_group;
static mbedtls_mpi s_host_secret;	   // ephemeral private scalar
static mbedtls_ecp_point s_host_point; // ephemeral public point
static bool s_ctx_live;				   // contexts above are initialised
static bool s_key_ready;			   // a key pair exists for verify_pub_key()

static void lt_slk_ctx_release(void) {
	if (s_ctx_live) {
		mbedtls_ecp_point_free(&s_host_point);
		mbedtls_mpi_free(&s_host_secret);
		mbedtls_ecp_group_free(&s_group);
		mbedtls_ctr_drbg_free(&s_drbg);
		mbedtls_entropy_free(&s_entropy);
	}
	s_ctx_live	= false;
	s_key_ready = false;
}

static void lt_slk_ctx_init(void) {
	lt_slk_ctx_release();
	mbedtls_entropy_init(&s_entropy);
	mbedtls_ctr_drbg_init(&s_drbg);
	mbedtls_ecp_group_init(&s_group);
	mbedtls_mpi_init(&s_host_secret);
	mbedtls_ecp_point_init(&s_host_point);
	s_ctx_live = true;
}

static void lt_slk_reverse(uint8_t *buf, size_t len) {
	for (size_t lo = 0, hi = len - 1; lo < hi; lo++, hi--) {
		uint8_t tmp = buf[lo];
		buf[lo]		= buf[hi];
		buf[hi]		= tmp;
	}
}

static int lt_slk_hmac_sha512(const uint8_t *key, const uint8_t *data, size_t len, uint8_t *mac) {
	return mbedtls_md_hmac(
		mbedtls_md_info_from_type(MBEDTLS_MD_SHA512),
		key,
		SL_WFX_SECURE_LINK_MAC_KEY_LENGTH,
		data,
		len,
		mac
	);
}

sl_status_t sl_wfx_host_get_secure_link_mac_key(uint8_t *sl_mac_key) {
	memcpy(sl_mac_key, LT_SLK_MAC_KEY_ADDR, SL_WFX_SECURE_LINK_MAC_KEY_LENGTH);
	for (uint32_t i = 0; i < SL_WFX_SECURE_LINK_MAC_KEY_LENGTH; i++) {
		if (sl_mac_key[i] != 0xFF)
			return SL_STATUS_OK;
	}
	return SL_STATUS_WIFI_SECURE_LINK_MAC_KEY_ERROR; // never provisioned
}

sl_status_t sl_wfx_host_compute_pub_key(
	sl_wfx_securelink_exchange_pub_keys_req_body_t *request,
	const uint8_t *sl_mac_key
) {
	static const unsigned char personalization[] = "lt-wfx-securelink";

	lt_slk_ctx_init();
	int ret =
		mbedtls_ctr_drbg_seed(&s_drbg, mbedtls_entropy_func, &s_entropy, personalization, sizeof(personalization));
	if (ret == 0)
		ret = mbedtls_ecp_group_load(&s_group, MBEDTLS_ECP_DP_CURVE25519);
	// The public key is the u-coordinate (point.X) of the ephemeral pair.
	if (ret == 0)
		ret = mbedtls_ecdh_gen_public(&s_group, &s_host_secret, &s_host_point, mbedtls_ctr_drbg_random, &s_drbg);
	if (ret == 0)
		ret = mbedtls_mpi_write_binary(&s_host_point.X, request->host_pub_key, SL_WFX_HOST_PUB_KEY_SIZE);
	if (ret == 0) {
		lt_slk_reverse(request->host_pub_key, SL_WFX_HOST_PUB_KEY_SIZE);
		ret =
			lt_slk_hmac_sha512(sl_mac_key, request->host_pub_key, SL_WFX_HOST_PUB_KEY_SIZE, request->host_pub_key_mac);
	}
	if (ret != 0) {
		lt_slk_ctx_release();
		return SL_STATUS_WIFI_SECURE_LINK_EXCHANGE_FAILED;
	}
	s_key_ready = true;
	return SL_STATUS_OK;
}

sl_status_t sl_wfx_host_verify_pub_key(
	sl_wfx_securelink_exchange_pub_keys_ind_t *response_packet,
	const uint8_t *sl_mac_key,
	uint8_t *sl_host_pub_key
) {
	SL_WFX_UNUSED_PARAMETER(sl_host_pub_key);

	uint8_t mac[SL_WFX_NCP_PUB_KEY_MAC_SIZE];
	uint8_t shared_le[SL_WFX_HOST_PUB_KEY_SIZE];
	uint8_t digest[32];
	mbedtls_ecp_point peer;
	mbedtls_mpi shared;
	sl_status_t status = SL_STATUS_WIFI_SECURE_LINK_EXCHANGE_FAILED;

	if (!s_key_ready)
		return SL_STATUS_WIFI_SECURE_LINK_EXCHANGE_FAILED;
	if (xSemaphoreTake(s_slk_mutex, portMAX_DELAY) != pdTRUE)
		return SL_STATUS_WIFI_SECURE_LINK_EXCHANGE_FAILED;

	mbedtls_ecp_point_init(&peer);
	mbedtls_mpi_init(&shared);

	// Authenticate the device public key before using it.
	if (lt_slk_hmac_sha512(sl_mac_key, response_packet->body.ncp_pub_key, SL_WFX_NCP_PUB_KEY_SIZE, mac) != 0)
		goto out;
	if (memcmp(mac, response_packet->body.ncp_pub_key_mac, SL_WFX_NCP_PUB_KEY_MAC_SIZE) != 0)
		goto out;

	// Device u-coordinate as an affine Montgomery point (Z = 1).
	lt_slk_reverse(response_packet->body.ncp_pub_key, SL_WFX_NCP_PUB_KEY_SIZE);
	if (mbedtls_mpi_read_binary(&peer.X, response_packet->body.ncp_pub_key, SL_WFX_NCP_PUB_KEY_SIZE) != 0)
		goto out;
	if (mbedtls_mpi_lset(&peer.Z, 1) != 0)
		goto out;

	if (mbedtls_ecdh_compute_shared(&s_group, &shared, &peer, &s_host_secret, mbedtls_ctr_drbg_random, &s_drbg) != 0)
		goto out;
	if (mbedtls_mpi_write_binary(&shared, shared_le, sizeof(shared_le)) != 0)
		goto out;
	lt_slk_reverse(shared_le, sizeof(shared_le));

	if (mbedtls_sha256(shared_le, sizeof(shared_le), digest, 0) != 0)
		goto out;
	memcpy(sl_wfx_context->secure_link_session_key, digest, SL_WFX_SECURE_LINK_SESSION_KEY_LENGTH);
	sl_wfx_context->secure_link_nonce.hp_packet_count = 0;
	sl_wfx_context->secure_link_nonce.rx_packet_count = 0;
	sl_wfx_context->secure_link_nonce.tx_packet_count = 0;
	status											  = SL_STATUS_OK;

out:
	mbedtls_platform_zeroize(mac, sizeof(mac));
	mbedtls_platform_zeroize(shared_le, sizeof(shared_le));
	mbedtls_platform_zeroize(digest, sizeof(digest));
	mbedtls_mpi_free(&shared); // zeroises its limbs
	mbedtls_ecp_point_free(&peer);
	xSemaphoreGive(s_slk_mutex);
	return status;
}

sl_status_t sl_wfx_host_free_crypto_context(void) {
	lt_slk_ctx_release(); // mbedtls_*_free() zeroise the key material
	return SL_STATUS_OK;
}

// length excludes the Secure Link header and the trailing CCM tag.
sl_status_t sl_wfx_host_decode_secure_link_data(uint8_t *buffer, uint32_t length, uint8_t *session_key) {
	mbedtls_ccm_context ccm;
	sl_status_t status = SL_STATUS_SECURITY_DECRYPT_ERROR;

	if (xSemaphoreTake(s_slk_mutex, portMAX_DELAY) != pdTRUE)
		return SL_STATUS_FAIL;

	// Receive nonce: only the rx counter is used.
	sl_wfx_nonce_t nonce  = {0, 0, 0};
	nonce.rx_packet_count = sl_wfx_context->secure_link_nonce.rx_packet_count;

	mbedtls_ccm_init(&ccm);
	if (mbedtls_ccm_setkey(&ccm, MBEDTLS_CIPHER_ID_AES, session_key, SL_WFX_SECURE_LINK_SESSION_KEY_BIT_COUNT) == 0 &&
		mbedtls_ccm_auth_decrypt(
			&ccm,
			length,
			(uint8_t *)&nonce,
			SL_WFX_SECURE_LINK_NONCE_SIZE_BYTES,
			NULL,
			0,
			buffer,
			buffer,
			buffer + length,
			SL_WFX_SECURE_LINK_CCM_TAG_SIZE
		) == 0) {
		status = SL_STATUS_OK;
	}

	mbedtls_ccm_free(&ccm);
	xSemaphoreGive(s_slk_mutex);
	return status;
}

// data_length excludes the Secure Link header and the trailing CCM tag; the
// driver supplies the tx nonce. The plaintext starts at the message id.
sl_status_t sl_wfx_host_encode_secure_link_data(
	sl_wfx_generic_message_t *buffer,
	uint32_t data_length,
	uint8_t *session_key,
	uint8_t *nonce
) {
	mbedtls_ccm_context ccm;
	sl_status_t status = SL_STATUS_FAIL;
	uint8_t *plaintext = (uint8_t *)&buffer->header.id;

	mbedtls_ccm_init(&ccm);
	if (mbedtls_ccm_setkey(&ccm, MBEDTLS_CIPHER_ID_AES, session_key, SL_WFX_SECURE_LINK_SESSION_KEY_BIT_COUNT) == 0 &&
		mbedtls_ccm_encrypt_and_tag(
			&ccm,
			data_length,
			nonce,
			SL_WFX_SECURE_LINK_NONCE_SIZE_BYTES,
			NULL,
			0,
			plaintext,
			plaintext,
			plaintext + data_length,
			SL_WFX_SECURE_LINK_CCM_TAG_SIZE
		) == 0) {
		status = SL_STATUS_OK;
	}

	mbedtls_ccm_free(&ccm);
	return status;
}

// ---- Renegotiation --------------------------------------------------------------
//
// The driver asks for a new session key from the bus task (inside
// sl_wfx_receive_frame(), host lock held) once a packet counter passes its
// watermark. The exchange is an ordinary driver command sequence that waits
// for confirmations the bus task delivers, so it runs here, under the command
// lock like every other command. Between the request and the end of the
// exchange the driver refuses all other requests (SL_STATUS_FAIL).

sl_status_t sl_wfx_host_schedule_secure_link_renegotiation(void) {
	if (s_slk_task == NULL)
		return SL_STATUS_FAIL;
	xTaskNotifyGive(s_slk_task);
	return SL_STATUS_OK;
}

static void lt_slk_task(void *arg) {
	(void)arg;
	for (;;) {
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		if (!lt_wfx_cmd_lock())
			continue;
		// A request made before a chip fault is stale: the chip has stopped,
		// or has been restarted (sl_wfx_init() negotiated a fresh key and
		// cleared the driver's renegotiation state).
		if (lt_wfx_host.chip_fault ||
			sl_wfx_context->secure_link_renegotiation_state != SL_WFX_SECURELINK_RENEGOTIATION_PENDING) {
			lt_wfx_cmd_unlock();
			continue;
		}
		sl_status_t st = sl_wfx_secure_link_renegotiate_session_key();
		lt_wfx_cmd_unlock();
		if (st != SL_STATUS_OK) {
			// Reported to the WiFi library; the driver requests another
			// renegotiation on the next frame past the watermark.
			lt_wfx_event_t ev = {.id = LT_WFX_EVENT_SLK_FAILED};
			ev.u.fault.code	  = st;
			lt_wfx_host_queue_event(&ev);
		}
	}
}

sl_status_t lt_wfx_slk_start(void) {
	if (s_slk_mutex == NULL)
		s_slk_mutex = xSemaphoreCreateMutex();
	if (s_slk_mutex == NULL)
		return SL_STATUS_ALLOCATION_FAILED;
	if (s_slk_task == NULL &&
		xTaskCreate(lt_slk_task, "wfx_slk", LT_SLK_TASK_STACK_WORDS, NULL, LT_SLK_TASK_PRIORITY, &s_slk_task) !=
			pdPASS) {
		s_slk_task = NULL;
		return SL_STATUS_ALLOCATION_FAILED;
	}
	return SL_STATUS_OK;
}

#endif // SL_WFX_USE_SECURE_LINK
