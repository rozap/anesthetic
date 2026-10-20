import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import type { Listing } from '../src/carpart.js';
import { notify } from '../src/discord.js';
import { Store, listingId } from '../src/store.js';

function listing(over: Partial<Listing> = {}): Listing {
  return {
    searchedYear: 2005,
    year: '2005',
    model: 'Volkswagen Touareg',
    part: 'Engine Assembly',
    description: '3.2L (VIN G, 5th digit, V6)',
    grade: 'A',
    price: '$550',
    priceUsd: 550,
    mileage: '153,599',
    distanceMiles: 231,
    dealer: 'Whiteys Wrecking',
    location: 'USA-WA(Spokane)',
    phone: '1-800-735-2376',
    stockNumber: 'HE214763',
    ...over,
  };
}

const storePath = () => join(mkdtempSync(join(tmpdir(), 'carpart-')), 'seen.json');

test('the id ignores price and description but not stock number', () => {
  const base = listing();
  assert.equal(listingId('s', base), listingId('s', { ...base, price: '$400', priceUsd: 400 }));
  assert.equal(listingId('s', base), listingId('s', { ...base, description: 'PRICE REDUCED' }));
  assert.notEqual(listingId('s', base), listingId('s', { ...base, stockNumber: 'OTHER' }));
  assert.notEqual(listingId('s', base), listingId('s2', base));
});

test('rows without a stock number fall back to description and mileage', () => {
  const a = listing({ stockNumber: '' });
  assert.equal(listingId('s', a), listingId('s', { ...a, price: '$1' }));
  assert.notEqual(listingId('s', a), listingId('s', { ...a, description: 'different' }));
});

test('a listing is new once, then reports price changes', () => {
  const store = Store.open(storePath());
  const first = store.merge('s', [listing(), listing({ stockNumber: 'B' })]);
  assert.equal(first.fresh.length, 2);
  assert.equal(first.repriced.length, 0);

  const second = store.merge('s', [listing(), listing({ stockNumber: 'B' })]);
  assert.equal(second.fresh.length, 0);
  assert.equal(second.repriced.length, 0);

  const third = store.merge('s', [listing({ price: '$400', priceUsd: 400 })]);
  assert.equal(third.fresh.length, 0);
  assert.equal(third.repriced.length, 1);
  assert.equal(third.repriced[0].from, '$550');
  assert.equal(third.repriced[0].listing.priceHistory.length, 2);
});

test('the store survives a round trip through disk', () => {
  const path = storePath();
  const store = Store.open(path);
  store.merge('s', [listing()]);
  store.save();

  const reopened = Store.open(path);
  assert.equal(reopened.size, 1);
  assert.equal(reopened.merge('s', [listing()]).fresh.length, 0);
});

test('notify posts one line per listing, cheapest first, split under 2000 characters', async () => {
  const received: Array<Record<string, any>> = [];
  const server = createServer((req, res) => {
    let body = '';
    req.on('data', (c) => (body += c));
    req.on('end', () => {
      received.push(JSON.parse(body));
      res.writeHead(204).end();
    });
  });
  await new Promise<void>((r) => server.listen(0, r));
  const { port } = server.address() as { port: number };

  const store = Store.open(storePath());
  const prices = [900, 300, null, 700, 100, 500, 800, 200, 600, 400, 1000, 1100];
  const { fresh } = store.merge(
    'touareg-vr6',
    prices.map((p, i) =>
      listing({
        stockNumber: `S${i}`,
        price: p == null ? '' : `$${p}`,
        priceUsd: p,
        photoUrl: i === 0 ? 'https://imageappoh.car-part.com/image?partGUID=0' : undefined,
        yardUrl: 'http://www.whiteys.example/',
      }),
    ),
  );

  const sent = await notify(
    { webhookUrl: `http://127.0.0.1:${port}/webhook`, mention: '<@&1>' },
    { searchName: 'touareg-vr6', fresh },
  );
  server.close();

  assert.equal(sent, received.length);
  assert.ok(sent >= 2, 'twelve listings do not fit in one message');
  for (const m of received) {
    assert.ok(m.content.length <= 2000);
    assert.equal(m.embeds, undefined);
  }
  assert.match(received[0].content, /^<@&1> \*\*touareg-vr6\*\*: 12 new listings\n/);

  const all = received.map((m) => m.content).join('\n');
  const order = [...all.matchAll(/^\*\*(\$\d+|call)\*\*/gm)].map((m) => m[1]);
  assert.deepEqual(order, [...prices.filter((p) => p != null).sort((a, b) => a! - b!).map((p) => `$${p}`), 'call']);
  assert.match(all, /\*\*\$900\*\* · 2005 Volkswagen Touareg Engine Assembly · 153,599 miles · grade A · 231 mi away · /);
  assert.match(all, /\[photo\]\(<https:\/\/imageappoh\.car-part\.com\/image\?partGUID=0>\) · \[yard site\]\(<http:\/\/www\.whiteys\.example\/>\)/);
  assert.match(all, /\n> 3\.2L \\\(VIN G, 5th digit, V6\\\)/);
});

test('a price change shows the old price', async () => {
  const received: Array<Record<string, any>> = [];
  const server = createServer((req, res) => {
    let body = '';
    req.on('data', (c) => (body += c));
    req.on('end', () => {
      received.push(JSON.parse(body));
      res.writeHead(204).end();
    });
  });
  await new Promise<void>((r) => server.listen(0, r));
  const { port } = server.address() as { port: number };

  const store = Store.open(storePath());
  store.merge('s', [listing()]);
  const { repriced } = store.merge('s', [listing({ price: '$400', priceUsd: 400 })]);
  await notify({ webhookUrl: `http://127.0.0.1:${port}/webhook` }, { searchName: 's', fresh: [], repriced });
  server.close();

  assert.match(received[0].content, /^\*\*s\*\*: 1 price change\n\*\*\$400\*\* \(was \$550\) · /);
});

test('notify sends nothing when there is nothing new', async () => {
  const sent = await notify({ webhookUrl: 'http://127.0.0.1:1/never' }, {
    searchName: 's',
    fresh: [],
  });
  assert.equal(sent, 0);
});

test('a stock number repeated within one run folds to the cheapest row', () => {
  const store = Store.open(storePath());
  const { fresh, repriced } = store.merge('s', [
    listing({ price: '$550', priceUsd: 550 }),
    listing({ price: '$400', priceUsd: 400 }),
  ]);
  assert.equal(fresh.length, 1);
  assert.equal(repriced.length, 0);
  assert.equal(fresh[0].price, '$400');

  // Same rows in the other order must land on the same listing.
  const reversed = store.merge('s', [
    listing({ price: '$400', priceUsd: 400 }),
    listing({ price: '$550', priceUsd: 550 }),
  ]);
  assert.equal(reversed.fresh.length, 0);
  assert.equal(reversed.repriced.length, 0);
});
