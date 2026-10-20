import { search, type Listing } from './carpart.js';
import { loadConfig, parseYears, resolveDiscord, type Config, type SearchConfig } from './config.js';
import { notify } from './discord.js';
import { Store, type StoredListing } from './store.js';

interface Args {
  [k: string]: string | boolean;
}

function parseArgs(argv: string[]): Args {
  const out: Args = {};
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (!a.startsWith('--')) continue;
    const eq = a.indexOf('=');
    if (eq > 0) out[a.slice(2, eq)] = a.slice(eq + 1);
    else if (argv[i + 1] && !argv[i + 1].startsWith('--')) out[a.slice(2)] = argv[++i];
    else out[a.slice(2)] = true;
  }
  return out;
}

const usage = `
Usage: npm start -- [options]

Runs every search in the config file, stores what it finds, and posts listings it
has not seen before to Discord.

  --config <path>   config file (default ./carpart.config.json)
  --search <name>   run only this search from the config
  --seed            record current listings as already seen, post nothing
  --dry-run         print what would be posted; do not post and do not write the store
  --pages <n>       override maxPages for every search
  --headed          show the browser
  --quiet           only print the summary lines
`;

function passesFilters(l: Listing, f: SearchConfig['notifyIf']): boolean {
  if (!f) return true;
  if (f.requirePrice && l.priceUsd == null) return false;
  if (f.maxPriceUsd != null && (l.priceUsd == null || l.priceUsd > f.maxPriceUsd)) return false;
  if (
    f.maxDistanceMiles != null &&
    (l.distanceMiles == null || l.distanceMiles > f.maxDistanceMiles)
  ) {
    return false;
  }
  if (f.descriptionMatches && !new RegExp(f.descriptionMatches, 'i').test(l.description)) {
    return false;
  }
  if (f.descriptionExcludes && new RegExp(f.descriptionExcludes, 'i').test(l.description)) {
    return false;
  }
  return true;
}

function printTable(listings: StoredListing[]): void {
  if (listings.length === 0) return;
  const cols: Array<[string, (l: StoredListing) => string]> = [
    ['Price', (l) => l.price || ''],
    ['Year', (l) => l.year || String(l.searchedYear)],
    ['Miles', (l) => l.mileage],
    ['Grade', (l) => l.grade],
    ['Dist', (l) => (l.distanceMiles == null ? '' : String(l.distanceMiles))],
    ['Dealer', (l) => l.dealer],
    ['Location', (l) => l.location],
    ['Phone', (l) => l.phone],
    ['Description', (l) => l.description.replace(/\n/g, ' ').slice(0, 60)],
  ];
  const sorted = [...listings].sort((a, b) => (a.priceUsd ?? Infinity) - (b.priceUsd ?? Infinity));
  const rows = [cols.map((c) => c[0]), ...sorted.map((l) => cols.map((c) => c[1](l)))];
  const widths = cols.map((_, i) => Math.max(...rows.map((r) => (r[i] ?? '').length)));
  for (const r of rows) console.log(r.map((c, i) => (c ?? '').padEnd(widths[i])).join('  ').trimEnd());
}

async function runSearch(cfg: Config, sc: SearchConfig, store: Store, args: Args): Promise<void> {
  const quiet = Boolean(args.quiet);
  const log = (m: string) => console.error(`[${sc.name}] ${m}`);

  const result = await search({
    year: parseYears(sc.year),
    model: sc.model,
    part: sc.part,
    location: sc.location ?? 'USA',
    zip: sc.zip,
    sort: sc.sort ?? 'price',
    interchange: sc.interchange ? new RegExp(sc.interchange, 'i') : undefined,
    maxPages: args.pages ? Number(args.pages) : (sc.maxPages ?? 5),
    headless: args.headed ? false : (cfg.browser?.headless ?? true),
    userDataDir: cfg.browser?.userDataDir ?? '.camoufox-profile',
    fingerprintOs: cfg.browser?.fingerprintOs,
    delayMs: cfg.browser?.delayMs ?? 1500,
    onLog: quiet ? () => {} : log,
  });

  const { fresh, repriced } = store.merge(sc.name, result.listings);
  const notifiable = fresh.filter((l) => passesFilters(l, sc.notifyIf));
  const notifiableReprices = repriced.filter((r) => passesFilters(r.listing, sc.notifyIf));

  console.error(
    `[${sc.name}] ${result.listings.length} listings, ${fresh.length} new, ` +
      `${repriced.length} repriced, ${notifiable.length} pass the notify filters`,
  );

  if (args.seed) {
    console.error(`[${sc.name}] seeding, nothing posted`);
  } else {
    const discord = resolveDiscord(cfg, sc);
    if (discord.enabled === false) {
      console.error(`[${sc.name}] discord disabled in config`);
    } else if (args['dry-run']) {
      console.error(`[${sc.name}] dry run, would post ${notifiable.length} listing(s)`);
    } else if (notifiable.length || notifiableReprices.length) {
      const sent = await notify(discord, {
        searchName: sc.name,
        fresh: notifiable,
        repriced: notifiableReprices,
      });
      console.error(`[${sc.name}] posted ${sent} Discord message(s)`);
    }
  }

  if (!quiet) printTable(notifiable);
}

const args = parseArgs(process.argv.slice(2));
if (args.help) {
  console.log(usage);
  process.exit(0);
}

async function main(): Promise<void> {
  const cfg = loadConfig(String(args.config ?? 'carpart.config.json'));
  const searches = args.search
    ? cfg.searches.filter((s) => s.name === String(args.search))
    : cfg.searches;
  if (searches.length === 0) throw new Error(`no search named "${String(args.search)}" in the config`);

  const storePath = cfg.storage?.path ?? 'data/seen.json';
  const store = Store.open(storePath);
  console.error(`[carpart] store ${storePath} holds ${store.size} listing(s)`);

  let failed = false;
  for (const sc of searches) {
    try {
      await runSearch(cfg, sc, store, args);
    } catch (err) {
      failed = true;
      console.error(`[${sc.name}] failed: ${(err as Error).message}`);
    } finally {
      // Persist after each search so a later failure cannot lose earlier results.
      if (!args['dry-run']) store.save();
    }
  }
  if (failed) process.exit(1);
}

await main().catch((err: Error) => {
  console.error(`[carpart] ${err.message}`);
  process.exit(1);
});
