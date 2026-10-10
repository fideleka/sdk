# Shared saved-network Wi-Fi fallback

Keira manages networks; Keira and Lilplayer use the same SDK credential storage
and cooperative `lilka::wifiConnection` selector. No radio or scan worker is activated by SDK startup. Consumers opt in explicitly on their owning
service/producer task. Non-networked firmware remains unchanged.

## Selection policy

1. Keep any already working connection; do not roam to a stronger AP.
2. Read the `network` namespace once for a connection round. Try `last_ssid`
   first if its saved credentials still exist, including an empty/open password.
3. After up to 10 seconds without IP, recheck success before stopping that
   association. Scan only if an untried saved candidate remains. Discovery uses
   a temporary 3072-byte-stack worker using Arduino's completion-owned scan
   (100..120 ms/channel). IDF 4.4 posts SCAN_DONE even for blocking scans;
   directly retrieving its list races Arduino's handler. The owner remains
   cooperative, with a 5-second polling deadline. No SSID/password is logged. Association attempts use RAM driver storage,
   even if Arduino previously initialized the adapter with its Flash default.
4. Match exact SSID bytes against saved full-SSID records. Ignore unknown APs;
   inspect at most 64 borrowed Arduino AP records without another copy,
   deduplicate repeated SSIDs and retain their
   strongest RSSI. Try other visible saved networks strongest first. Each SSID
   is attempted once per round, with up to 10 seconds per association.
5. Stop when one obtains IP or candidates are exhausted. Driver autoreconnect
   is disabled during selection so it cannot compete with the fallback order.

At most 16 indexed networks plus a recoverable legacy selected network are loaded.
The complete round has a 35-second polling budget, including scanning. Saved
candidate buffers/capacity are released after success, failure or cancellation;
only the selected credential remains for post-IP persistence. The adapter borrows
Arduino's contiguous result array and exposes at most 64 records; it allocates no
second result buffer. Arduino still allocates ALL discovered records (80 bytes
each on the installed ESP32-S3 ABI), in addition to the driver list and temporary
3072-byte task stack/TCB. Total scan memory is NOT capped.
Real driver calls aren't preempted by polling deadlines. BSSID/channel pinning and roaming
while connected are intentionally not added. A preferred hidden AP is tried
without a scan; non-preferred hidden APs cannot be discovered for fallback.

Keira polls once per second; failed rounds use 30/60/120/240/480/900-second
cooldowns, capped at 15 minutes. Success and explicit connection requests reset
the cooldown. An
explicit Connect in the management UI tries that chosen network only; a cancelled
or failed manual attempt does not silently choose another one. After successful
manual connection, later loss can use automatic fallback. Disconnect disables
automatic attempts for this session. Forget invalidates the RAM snapshot before
removing credentials, so an old cached password cannot be attempted afterward.
Opening Wi-Fi management pauses background selection to avoid concurrent scans
and preserves a working connection; leaving it resumes automatic recovery unless
Disconnect was selected.

Lilplayer polls on the existing network producer every 20 ms, honors stream
generation cancellation and retains its existing stream retry/backoff policy.
Socket/audio tasks are unchanged. Cancellation stops the owned association but
preserves a working connection. An in-flight short blocking scan drains and
frees its discarded result; no new scan starts until it has drained. This avoids
releasing Arduino results before its completion handler has finished.
Keira's explicit management scan uses the same processing-limited adapter; legacy script
Arduino scans are unchanged and must not run concurrently with a selector scan.

A successful fallback is remembered only after IP. Identical saved records,
passwords and `last_ssid` cause no writes. SDK `remember()` validates that the
credentials still exist and match the snapshot before saving; failure does not
break an already working radio stream. Keira saves under its existing NVS lock,
not in Wi-Fi event callbacks. Credentials remain in the original format and
legacy hash/password mirrors remain compatible with older firmware. Legacy
password-only entries with unknown SSID names must first be discovered/imported
by Keira; a hash alone cannot recover their names. Emoji SSIDs remain exact UTF-8.

## API and ownership

`NetworkCredentials` is a stateless SDK utility; caller owns an open Preferences
namespace and its NVS lock. `WiFiConnection` is a lazily used SDK singleton:
`load(prefs)`, `start(now)`, `poll(now)`, `cancel(disconnect)`, `remember(prefs)`.
The caller enables station mode and serializes these methods; never invoke them from Wi-Fi event callbacks.
Credential storage and candidate processing are bounded per round; no NVS access occurs
inside `poll()`, and no logging is performed by the selector.

Existing Arduino WiFiMulti was inspected locally. Its blocking scan/association
loop lacks the cooperative cancellation and shared storage/Forget semantics
needed by these consumers, so the SDK adds a bounded selector over the existing
installed Wi-Fi adapter rather than adding an external dependency.

## Verification

Run `python3 tests/wifi/run.py` for real SDK storage and selector sources under
signed/unsigned-char and normal/ASan/UBSan modes. Also run
`python3 tests/wifi_scan/run.py` for completion ownership, 200/3/0 discovered
APs, the 64-record processing cap, borrowed-result lifetime, cancellation drain
and task/scan failures. Its competing-consumer fixture fails against the old
IDF collector (14b237c) and passes against the corrected adapter. Keira's Wi-Fi host suite exercises
the actual service, fallback, successful-IP save, cancellation and Forget races.
Lilplayer's transport suite compiles these same SDK sources with the actual
producer/Playback. HAL tests model scans and IP events; they do not establish
ESP32 RF behavior, driver timing or stack/heap headroom. Target header syntax
checks are not firmware builds. Device tests must cover an unavailable preferred
AP, a wrong-password AP, a reachable backup, cancellation and later loss/recovery.
