import { readFileSync } from 'node:fs';
import type { SearchOptions } from './carpart.js';

export interface DiscordConfig {
  /** Channel webhook URL. A webhook already points at one server + channel. */
  webhookUrl?: string;
  /** Alternative to a webhook: a bot token plus the target channel id. */
  botToken?: string;
  channelId?: string;
  /** Prefixed to every message, e.g. "<@&1234>" to ping a role. */
  mention?: string;
  /** Skip posting; still updates the store. */
  enabled?: boolean;
}

export interface SearchConfig {
  /** Identifies the search in the store and in Discord messages. */
  name: string;
  /** 2004, "2004-2007", or [2004, 2006]. */
  year: number | string | number[];
  model: string;
  part: string;
  location?: string;
  zip?: string;
  sort?: SearchOptions['sort'];
  /** Regex string matched against the interchange variant labels. */
  interchange?: string;
  maxPages?: number;
  /** Only notify on listings that pass these. Everything is still stored. */
  notifyIf?: {
    maxPriceUsd?: number;
    maxDistanceMiles?: number;
    requirePrice?: boolean;
    descriptionMatches?: string;
    descriptionExcludes?: string;
  };
  /** Overrides the top-level discord block for this search. */
  discord?: DiscordConfig;
}

export interface Config {
  browser?: {
    headless?: boolean;
    userDataDir?: string;
    delayMs?: number;
    /** Fingerprint OS Camoufox presents: "windows" (default), "macos", "linux". */
    fingerprintOs?: SearchOptions['fingerprintOs'];
  };
  storage?: {
    /** JSON file holding every listing seen so far. */
    path?: string;
  };
  discord?: DiscordConfig;
  searches: SearchConfig[];
}

/** `${VAR}` anywhere in a string is replaced with that environment variable. */
function expandEnv<T>(value: T): T {
  if (typeof value === 'string') {
    return value.replace(/\$\{([A-Z0-9_]+)\}/gi, (whole, name: string) => {
      const v = process.env[name];
      if (v === undefined) throw new Error(`config references ${whole} but ${name} is not set`);
      return v;
    }) as unknown as T;
  }
  if (Array.isArray(value)) return value.map(expandEnv) as unknown as T;
  if (value && typeof value === 'object') {
    return Object.fromEntries(
      Object.entries(value as Record<string, unknown>).map(([k, v]) => [k, expandEnv(v)]),
    ) as T;
  }
  return value;
}

export function parseYears(spec: number | string | number[]): number[] {
  if (typeof spec === 'number') return [spec];
  if (Array.isArray(spec)) return spec;
  const range = spec.match(/^(\d{4})\s*-\s*(\d{4})$/);
  if (range) {
    const [a, b] = [Number(range[1]), Number(range[2])];
    if (b < a) throw new Error(`bad year range: ${spec}`);
    return Array.from({ length: b - a + 1 }, (_, i) => a + i);
  }
  const years = spec.split(',').map((y) => Number(y.trim()));
  if (years.some((y) => !Number.isInteger(y))) throw new Error(`bad year spec: ${spec}`);
  return years;
}

export function resolveDiscord(cfg: Config, search: SearchConfig): DiscordConfig {
  return { ...(cfg.discord ?? {}), ...(search.discord ?? {}) };
}

export function loadConfig(path: string): Config {
  let raw: string;
  try {
    raw = readFileSync(path, 'utf8');
  } catch {
    throw new Error(`cannot read config at ${path} (copy carpart.config.example.json to start)`);
  }
  // Strip // comments so the config can be annotated.
  const stripped = raw.replace(/^\s*\/\/.*$/gm, '');
  const cfg = expandEnv(JSON.parse(stripped)) as Config;

  if (!Array.isArray(cfg.searches) || cfg.searches.length === 0) {
    throw new Error('config needs a non-empty "searches" array');
  }
  const names = new Set<string>();
  for (const s of cfg.searches) {
    if (!s.name) throw new Error('every search needs a "name"');
    if (names.has(s.name)) throw new Error(`duplicate search name: ${s.name}`);
    names.add(s.name);
    if (!s.model || !s.part || s.year === undefined) {
      throw new Error(`search "${s.name}" needs year, model and part`);
    }
    parseYears(s.year);

    const d = resolveDiscord(cfg, s);
    if (d.enabled !== false && !d.webhookUrl && !(d.botToken && d.channelId)) {
      throw new Error(
        `search "${s.name}" has no Discord target: set discord.webhookUrl, or discord.botToken plus discord.channelId, or discord.enabled=false`,
      );
    }
  }
  return cfg;
}
