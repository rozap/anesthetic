import type { DiscordConfig } from './config.js';
import type { StoredListing } from './store.js';

// Plain message text rather than embeds: embeds are fixed at ~520px wide and
// lay fields out three to a row, so each listing ran five or six rows tall.
const MAX_CONTENT = 2000;
const MAX_DESCRIPTION = 200;
// Webhook message flag: do not unfurl link previews under the listings.
const SUPPRESS_EMBEDS = 1 << 2;

interface Entry {
  listing: StoredListing;
  /** Previous price, set for a price change. */
  from?: string;
}

/** Backslash-escapes Discord markdown so yard text cannot bold or strike a line. */
function esc(s: string): string {
  return s.replace(/([\\*_~`|>[\]()])/g, '\\$1');
}

function line(e: Entry): string {
  const l = e.listing;
  const price = `**${esc(l.price || 'call')}**${e.from != null ? ` (was ${esc(e.from || 'call')})` : ''}`;
  const facts = [
    price,
    esc([l.year, l.model, l.part].filter(Boolean).join(' ')),
    l.mileage && `${esc(l.mileage)} miles`,
    l.grade && `grade ${esc(l.grade)}`,
    l.distanceMiles != null && `${l.distanceMiles} mi away`,
    esc([l.dealer, l.location].filter(Boolean).join(', ')),
    l.phone && esc(l.phone),
    l.stockNumber && `stock ${esc(l.stockNumber)}`,
    l.photoUrl && `[photo](<${l.photoUrl}>)`,
    l.yardUrl && `[yard site](<${l.yardUrl}>)`,
  ].filter(Boolean);

  let text = facts.join(' · ');
  if (l.description) {
    const d = l.description.length > MAX_DESCRIPTION ? `${l.description.slice(0, MAX_DESCRIPTION - 1)}…` : l.description;
    text += `\n> ${esc(d)}`;
  }
  return text.slice(0, MAX_CONTENT);
}

/** Cheapest first; listings priced "call" go last. */
function byPrice(a: Entry, b: Entry): number {
  return (a.listing.priceUsd ?? Infinity) - (b.listing.priceUsd ?? Infinity);
}

async function post(cfg: DiscordConfig, body: unknown): Promise<void> {
  const [url, headers] = cfg.webhookUrl
    ? [cfg.webhookUrl, { 'content-type': 'application/json' }]
    : [
        `https://discord.com/api/v10/channels/${cfg.channelId}/messages`,
        { 'content-type': 'application/json', authorization: `Bot ${cfg.botToken}` },
      ];

  for (let attempt = 0; attempt < 5; attempt++) {
    const res = await fetch(url, { method: 'POST', headers, body: JSON.stringify(body) });
    if (res.ok) return;

    if (res.status === 429) {
      const retryAfter = Number(res.headers.get('retry-after') ?? '1');
      await new Promise((r) => setTimeout(r, Math.min(retryAfter * 1000 + 250, 30_000)));
      continue;
    }
    const text = await res.text().catch(() => '');
    throw new Error(`Discord returned ${res.status}: ${text.slice(0, 300)}`);
  }
  throw new Error('Discord rate limited five times in a row, giving up');
}

/**
 * Posts one line per listing, cheapest first, packed into as few messages as
 * the 2000-character limit allows. Returns the number of messages sent.
 */
export async function notify(
  cfg: DiscordConfig,
  opts: {
    searchName: string;
    fresh: StoredListing[];
    repriced?: Array<{ listing: StoredListing; from: string }>;
  },
): Promise<number> {
  const repriced = opts.repriced ?? [];
  const entries: Entry[] = [...opts.fresh.map((listing) => ({ listing })), ...repriced].sort(byPrice);
  if (entries.length === 0) return 0;

  const summary = [
    opts.fresh.length ? `${opts.fresh.length} new listing${opts.fresh.length === 1 ? '' : 's'}` : '',
    repriced.length ? `${repriced.length} price change${repriced.length === 1 ? '' : 's'}` : '',
  ]
    .filter(Boolean)
    .join(' and ');

  const messages: string[] = [];
  let current = `${cfg.mention ? `${cfg.mention} ` : ''}**${esc(opts.searchName)}**: ${summary}`;
  for (const text of entries.map(line)) {
    if (current.length + 1 + text.length > MAX_CONTENT) {
      messages.push(current);
      current = text;
    } else {
      current += `\n${text}`;
    }
  }
  messages.push(current);

  for (let i = 0; i < messages.length; i++) {
    await post(cfg, {
      content: messages[i],
      flags: SUPPRESS_EMBEDS,
      allowed_mentions: { parse: ['roles', 'users'] },
    });
    if (i + 1 < messages.length) await new Promise((r) => setTimeout(r, 750));
  }
  return messages.length;
}
