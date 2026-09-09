import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, renameSync, writeFileSync } from 'node:fs';
import { dirname } from 'node:path';
import type { Listing } from './carpart.js';

export interface StoredListing extends Listing {
  id: string;
  search: string;
  firstSeen: string;
  lastSeen: string;
  /** Appended to whenever the posted price changes. */
  priceHistory: Array<{ at: string; price: string }>;
}

interface StoreFile {
  version: 1;
  listings: Record<string, StoredListing>;
}

/**
 * A yard's stock number is unique within that yard, so dealer + stock number
 * identifies a listing across runs. A few rows carry no stock number; those fall
 * back to description and mileage. Price and description are deliberately left
 * out of the id so a re-priced or re-worded listing stays the same listing.
 */
export function listingId(search: string, l: Listing): string {
  const stock = l.stockNumber.trim();
  const parts = [
    search,
    l.dealer.trim().toLowerCase(),
    l.location.trim().toLowerCase(),
    l.year,
    l.part.trim().toLowerCase(),
    stock
      ? `stock:${stock.toLowerCase()}`
      : `alt:${l.description.trim().toLowerCase()}|${l.mileage}`,
  ];
  return createHash('sha1').update(parts.join(' ')).digest('hex').slice(0, 16);
}

export class Store {
  private data: StoreFile;

  private constructor(
    private readonly path: string,
    data: StoreFile,
  ) {
    this.data = data;
  }

  static open(path: string): Store {
    try {
      const parsed = JSON.parse(readFileSync(path, 'utf8')) as StoreFile;
      if (parsed.version !== 1 || typeof parsed.listings !== 'object') {
        throw new Error(`unrecognised store format in ${path}`);
      }
      return new Store(path, parsed);
    } catch (err: unknown) {
      if ((err as NodeJS.ErrnoException).code === 'ENOENT') {
        return new Store(path, { version: 1, listings: {} });
      }
      throw err;
    }
  }

  get size(): number {
    return Object.keys(this.data.listings).length;
  }

  has(id: string): boolean {
    return id in this.data.listings;
  }

  /**
   * Folds a run's listings in. Returns the ones not seen before, plus the ones
   * whose posted price moved.
   */
  merge(
    search: string,
    listings: Listing[],
    now = new Date().toISOString(),
  ): { fresh: StoredListing[]; repriced: Array<{ listing: StoredListing; from: string }> } {
    const fresh: StoredListing[] = [];
    const repriced: Array<{ listing: StoredListing; from: string }> = [];

    // A yard occasionally lists the same stock number twice at different prices.
    // Keep the cheapest priced row so the choice does not depend on the order
    // the site happened to return, which would look like a price change.
    const byId = new Map<string, Listing>();
    for (const l of listings) {
      const id = listingId(search, l);
      const kept = byId.get(id);
      if (!kept || (l.priceUsd ?? Infinity) < (kept.priceUsd ?? Infinity)) byId.set(id, l);
    }

    for (const [id, l] of byId) {
      const existing = this.data.listings[id];
      if (!existing) {
        const stored: StoredListing = {
          ...l,
          id,
          search,
          firstSeen: now,
          lastSeen: now,
          priceHistory: [{ at: now, price: l.price }],
        };
        this.data.listings[id] = stored;
        fresh.push(stored);
        continue;
      }

      const previousPrice = existing.price;
      const updated: StoredListing = {
        ...existing,
        ...l,
        id,
        search,
        firstSeen: existing.firstSeen,
        lastSeen: now,
        priceHistory: existing.priceHistory,
      };
      if (l.price !== previousPrice) {
        updated.priceHistory = [...existing.priceHistory, { at: now, price: l.price }];
        repriced.push({ listing: updated, from: previousPrice });
      }
      this.data.listings[id] = updated;
    }
    return { fresh, repriced };
  }

  save(): void {
    mkdirSync(dirname(this.path), { recursive: true });
    const tmp = `${this.path}.tmp`;
    writeFileSync(tmp, JSON.stringify(this.data, null, 2));
    renameSync(tmp, this.path);
  }
}
