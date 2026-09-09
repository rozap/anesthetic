import type { DiscordConfig } from './config.js';
import type { StoredListing } from './store.js';

interface Embed {
  title: string;
  description?: string;
  color?: number;
  fields: Array<{ name: string; value: string; inline?: boolean }>;
  footer?: { text: string };
  timestamp?: string;
}

const GREEN = 0x27ae60;
const AMBER = 0xf2c744;
const EMBEDS_PER_MESSAGE = 10;

function field(name: string, value: string, inline = true) {
  return { name, value: value.trim() || 'n/a', inline };
}

function listingEmbed(l: StoredListing, color: number): Embed {
  const title = [l.year, l.part, l.model].filter(Boolean).join(' ').slice(0, 256);
  return {
    title,
    description: l.description ? l.description.slice(0, 500) : undefined,
    color,
    fields: [
      field('Price', l.price || 'call'),
      field('Miles', l.mileage),
      field('Grade', l.grade),
      field('Distance', l.distanceMiles == null ? 'n/a' : `${l.distanceMiles} mi`),
      field('Stock #', l.stockNumber),
      field('Phone', l.phone),
      field('Yard', `${l.dealer}${l.location ? ` — ${l.location}` : ''}`.slice(0, 1024), false),
    ],
    footer: { text: `${l.search} · ${l.id}` },
    timestamp: l.firstSeen,
  };
}

function repriceEmbed(l: StoredListing, from: string): Embed {
  const e = listingEmbed(l, AMBER);
  e.fields.unshift(field('Price change', `${from || 'call'} → ${l.price || 'call'}`, false));
  return e;
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

/** Posts one message per batch of ten embeds. Returns the number of messages sent. */
export async function notify(
  cfg: DiscordConfig,
  opts: {
    searchName: string;
    fresh: StoredListing[];
    repriced?: Array<{ listing: StoredListing; from: string }>;
  },
): Promise<number> {
  const repriced = opts.repriced ?? [];
  const embeds = [
    ...opts.fresh.map((l) => listingEmbed(l, GREEN)),
    ...repriced.map(({ listing, from }) => repriceEmbed(listing, from)),
  ];
  if (embeds.length === 0) return 0;

  const summary = [
    opts.fresh.length ? `${opts.fresh.length} new listing${opts.fresh.length === 1 ? '' : 's'}` : '',
    repriced.length ? `${repriced.length} price change${repriced.length === 1 ? '' : 's'}` : '',
  ]
    .filter(Boolean)
    .join(' and ');

  let sent = 0;
  for (let i = 0; i < embeds.length; i += EMBEDS_PER_MESSAGE) {
    const batch = embeds.slice(i, i + EMBEDS_PER_MESSAGE);
    const first = i === 0;
    const content = first
      ? `${cfg.mention ? `${cfg.mention} ` : ''}**${opts.searchName}**: ${summary}`
      : undefined;
    await post(cfg, { content, embeds: batch, allowed_mentions: { parse: ['roles', 'users'] } });
    sent++;
    if (i + EMBEDS_PER_MESSAGE < embeds.length) await new Promise((r) => setTimeout(r, 750));
  }
  return sent;
}
