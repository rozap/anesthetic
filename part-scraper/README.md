# part-scraper

Watches car-part.com for used-part listings and posts new ones to Discord.

## How the site works

The search is a plain form POST to `/cgi-bin/search.cgi`, but Cloudflare puts a JS
challenge in front of that endpoint, so raw `curl`/`fetch` gets a 403 no matter how the
headers are shaped. Stock headless Chrome does not get through either; Cloudflare
fingerprints it and serves an interstitial that never resolves. The scraper therefore
drives Camoufox, a patched Firefox that generates a fresh fingerprint at launch, and
waits the interstitial out. Everything after that is ordinary HTML parsing.

The flow is three steps:

1. Home page: pick year, model, part, location, sort, zip. Six hidden `tk1..tk6` tokens
   ride along with the form; they are per-session, so the form has to be submitted from
   the page it came from rather than replayed.
2. Interchange page: radio buttons for the part variants (for a Touareg engine, 3.2L
   VIN C, 3.2L VIN G, 4.2L, 5.0L V10). The `interchange` regex picks one.
3. Results: one table per page, paginated by numbered `userPage=N` links.

Car-Part searches one model year at a time. A year range runs them in sequence in the
same browser session and merges the rows.

## Setup

```
npm install
cp carpart.config.example.json carpart.config.json
export DISCORD_WEBHOOK_URL='https://discord.com/api/webhooks/...'
npm start -- --seed      # record what is listed today without posting it
npm start                # from here on, post anything new
```

`npm install` pulls in `camoufox-js`, but the browser itself is a separate ~660MB
download plus a ~65MB GeoIP database:

```
npx camoufox-js fetch
```

No system Chrome or Playwright browser download is needed. The profile lives in
`.camoufox-profile/` so the Cloudflare clearance cookie survives between runs. The
fingerprint OS is pinned to Windows via `browser.fingerprintOs`, because a profile whose
fingerprint changes shape between runs is itself a signal; change it only when starting
a fresh profile.

If runs start coming back as `Cloudflare challenge did not clear`, delete
`.camoufox-profile/` and let the next run build a new one.

Get the webhook URL from Discord: Server Settings, Integrations, Webhooks, New Webhook,
pick the channel, Copy Webhook URL. A webhook is already bound to one server and one
channel. To use a bot instead, set `discord.botToken` and `discord.channelId`; the bot
needs View Channel and Send Messages there.

The first run must be `--seed`, otherwise every listing currently on the site counts as
new and you get a few hundred embeds.

## Running it on a schedule

```
*/30 * * * * cd /home/chris/workspace/anesthetic/part-scraper && \
  DISCORD_WEBHOOK_URL=... /usr/bin/npm start --silent >> log/run.log 2>&1
```

Every 30 minutes is plenty; yards update inventory daily at best.

## Config

`carpart.config.json`, with `${VAR}` in any string replaced from the environment.
Lines starting with `//` are stripped, so the file can be annotated.

```json
{
  "browser": { "headless": true, "userDataDir": ".pw-profile", "delayMs": 1500 },
  "storage": { "path": "data/seen.json" },
  "discord": { "webhookUrl": "${DISCORD_WEBHOOK_URL}", "mention": "" },
  "searches": [
    {
      "name": "touareg-vr6",
      "year": "2004-2007",
      "model": "Volkswagen Touareg",
      "part": "Engine",
      "location": "USA",
      "zip": "98103",
      "sort": "price",
      "interchange": "3\\.2",
      "maxPages": 5,
      "notifyIf": {
        "maxPriceUsd": 2000,
        "maxDistanceMiles": 1500,
        "descriptionExcludes": "core|parts only|junk"
      }
    }
  ]
}
```

| key | meaning |
|---|---|
| `searches[].name` | identifies the search in the store and in the Discord message |
| `searches[].year` | `2004`, `"2004-2007"`, or `[2004, 2006]` |
| `searches[].model` / `part` | spelled as in the site's dropdowns |
| `searches[].location` | `USA`, `All States`, a region (`Northwest`), or a state name |
| `searches[].zip` | needed for the distance column and for `"sort": "zip"` |
| `searches[].sort` | `zip`, `grade`, `condition`, `price`, `year` |
| `searches[].interchange` | regex matched against the variant labels |
| `searches[].maxPages` | result pages per year, default 5 |
| `searches[].notifyIf` | narrows what gets posted; everything found is still stored |
| `searches[].discord` | per-search override of the top-level Discord block |
| `discord.mention` | prefixed to each message, e.g. `<@&ROLE_ID>` |
| `discord.enabled` | `false` to scrape and store without posting |
| `browser.userDataDir` | profile directory, default `.camoufox-profile` |
| `browser.fingerprintOs` | `windows` (default), `macos`, `linux` |

`notifyIf` accepts `maxPriceUsd`, `maxDistanceMiles`, `requirePrice`,
`descriptionMatches` and `descriptionExcludes` (the last two are regexes).

## Flags

| flag | meaning |
|---|---|
| `--config <path>` | config file, default `./carpart.config.json` |
| `--search <name>` | run only one search from the config |
| `--seed` | record current listings as seen, post nothing |
| `--dry-run` | print what would be posted; post nothing, write nothing |
| `--pages <n>` | override `maxPages` for every search |
| `--headed` | show the browser, useful when a challenge needs a look |
| `--quiet` | summary lines only |

## Identity and storage

`data/seen.json` holds every listing ever seen, keyed by a sha1 of search name, dealer,
location, donor year, part and stock number. A yard's stock number is unique within that
yard, so that combination is stable across runs. Price and description are deliberately
left out of the key, so a re-priced or re-worded listing stays the same listing and does
not re-notify; the change is reported as a price change instead, and each listing keeps
its `priceHistory`.

Roughly 1 percent of rows carry no stock number. Those fall back to description plus
mileage, which is stable in practice but will look like a new listing if the yard edits
the description.

The store is written after each search, through a temp file and a rename, so an
interrupted run cannot leave a half-written file.

## Development

```
npm test          # store identity, merge behaviour, Discord batching (no network)
npx tsc --noEmit  # typecheck
```

`src/carpart.ts` exports `search(opts)` if you want the listings rather than the
notifications. Requests are spaced by `browser.delayMs`, default 1.5s. Keep it polite.
