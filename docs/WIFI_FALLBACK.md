# Shared saved-network Wi-Fi fallback

Keira manages networks; Keira and Lilplayer use the same SDK credential storage
and cooperative `lilka::wifiConnection` selector. No radio is activated and no
new task is created by SDK startup. Consumers opt in explicitly on their owning
service/producer task. Non-networked firmware remains unchanged.

## Selection policy

1. Keep any already working connection; do not roam to a stronger AP.
2. Read the `network` namespace once for a connection round. Try `last_ssid`
   first if its saved credentials still exist, including an empty/open password.
3. After up to 10 seconds without IP, stop that association and scan asynchronously.
   Scanning has a 5-second polling deadline. No SSID/password is logged. Association attempts use RAM driver storage,
   even if Arduino previously initialized the adapter with its Flash default.
4. Match exact SSID bytes against saved full-SSID records. Ignore unknown APs;
   inspect at most 64 scan results, deduplicate repeated SSIDs and retain their
   strongest RSSI. Try other visible saved networks strongest first. Each SSID
   is attempted once per round, with up to 10 seconds per association.
5. Stop when one obtains IP or candidates are exhausted. Driver autoreconnect
   is disabled during selection so it cannot compete with the fallback order.

At most 16 indexed networks plus a recoverable legacy selected network are loaded.
Worst-case polling budget is 17 * 10 seconds + 5 seconds per round; real driver
calls aren't preempted by these deadlines. BSSID/channel pinning and roaming
while connected are intentionally not added. A preferred hidden AP is tried
without a scan; non-preferred hidden APs cannot be discovered for fallback.

Keira polls once per second and retries a failed round after 30 seconds. An
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
Socket/audio tasks are unchanged. Cancellation tears down only the selector's
owned association/scan, not a separate working connection.

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
All storage/scanning/selection work is bounded per round; no NVS access occurs
inside `poll()`, and no logging is performed by the selector.

Existing Arduino WiFiMulti was inspected locally. Its blocking scan/association
loop lacks the cooperative cancellation and shared storage/Forget semantics
needed by these consumers, so the SDK adds a bounded selector over the existing
installed Wi-Fi adapter rather than adding an external dependency.

## Verification

Run `python3 tests/wifi/run.py` for real SDK storage and selector sources under
signed/unsigned-char and normal/ASan/UBSan modes. Keira's Wi-Fi host suite exercises
the actual service, fallback, successful-IP save, cancellation and Forget races.
Lilplayer's transport suite compiles these same SDK sources with the actual
producer/Playback. HAL tests model scans and IP events; they do not establish
ESP32 RF behavior, driver timing or stack/heap headroom. Target header syntax
checks are not firmware builds. Device tests must cover an unavailable preferred
AP, a wrong-password AP, a reachable backup, cancellation and later loss/recovery.
