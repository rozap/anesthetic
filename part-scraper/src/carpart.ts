import { Camoufox } from 'camoufox-js';
import type { BrowserContext, Page } from 'playwright-core';

export interface SearchOptions {
  /** Model year(s) to search. Car-Part searches one year at a time; several are run in sequence. */
  year: number | number[];
  /** Exactly as listed in the site's model dropdown, e.g. "Volkswagen Touareg". */
  model: string;
  /** Exactly as listed in the part dropdown, e.g. "Engine". */
  part: string;
  /** Region value from the location dropdown: a state name, a region, "USA", "All States". */
  location?: string;
  zip?: string;
  sort?: 'zip' | 'grade' | 'condition' | 'price' | 'year';
  /**
   * Car-Part first asks which interchange variant (engine option) you want.
   * The first option whose label matches is selected. Omit to take the default.
   */
  interchange?: string | RegExp;
  maxPages?: number;
  headless?: boolean;
  /** Persisted browser profile, so the Cloudflare clearance cookie survives runs. */
  userDataDir?: string;
  /** Fingerprint OS Camoufox presents. Pinned per profile; changing it between runs is itself a signal. */
  fingerprintOs?: 'windows' | 'macos' | 'linux';
  /** Milliseconds between page fetches. */
  delayMs?: number;
  onLog?: (msg: string) => void;
}

export interface Listing {
  /** The year that was searched (the donor vehicle year is `year`). */
  searchedYear: number;
  year: string;
  model: string;
  part: string;
  description: string;
  grade: string;
  price: string;
  priceUsd: number | null;
  mileage: string;
  distanceMiles: number | null;
  dealer: string;
  location: string;
  phone: string;
  stockNumber: string;
  /** Car-Part photo of the part, when the yard uploaded one. */
  photoUrl?: string;
  /** The yard's own website or Car-Part storefront. */
  yardUrl?: string;
}

export interface SearchResult {
  interchangeOptions: string[];
  interchangeChosen: string | null;
  pages: number;
  listings: Listing[];
}

declare function radioLabel(el: HTMLInputElement): string;

/** Injected into every page: the interchange radios are labelled by <label for=id>. */
const RADIO_LABEL_HELPER = `window.radioLabel = function (el) {
  const byFor = el.id ? document.querySelector('label[for="' + el.id + '"]') : null;
  const wrap = el.closest('label');
  return ((byFor || wrap)?.innerText || '').replace(/\\s+/g, ' ').trim();
};`;

const HOME = 'https://www.car-part.com/';
const DEFAULT_PROFILE = '.camoufox-profile';

/** Cloudflare serves an interstitial that resolves itself; wait it out. */
async function settle(page: Page, log: (m: string) => void): Promise<void> {
  for (let i = 0; i < 40; i++) {
    const title = await page.title().catch(() => '');
    const body = await page.evaluate(() => document.body?.innerText ?? '').catch(() => '');
    const challenged =
      /just a moment|attention required|performing security verification/i.test(title + body);
    if (!challenged) return;
    if (i === 0) log('waiting out Cloudflare check...');
    await page.waitForTimeout(1500);
  }
  throw new Error('Cloudflare challenge did not clear');
}

function money(text: string): number | null {
  const m = text.replace(/,/g, '').match(/\$?\s*(\d+(?:\.\d+)?)/);
  return m ? Number(m[1]) : null;
}

function distance(text: string): number | null {
  const m = text.replace(/,/g, '').match(/(\d+(?:\.\d+)?)\s*mile/i);
  return m ? Number(m[1]) : null;
}

/** Read the listing rows out of a results page. */
async function parseListings(page: Page, searchedYear: number): Promise<Listing[]> {
  const rows = await page.evaluate(() => {
    // Note: no named helper functions in here; the TS loader's keepNames
    // transform injects a __name call that does not exist in page scope.
    const trs = [...document.querySelectorAll('tr')];
    const headerRow = trs.find(
      (tr) =>
        tr.querySelectorAll('td,th').length >= 5 &&
        /stock/i.test((tr as HTMLElement).innerText || '') &&
        /price/i.test((tr as HTMLElement).innerText || ''),
    );
    if (!headerRow) return { headers: [] as string[], body: [] as string[][], links: [] as Array<Array<{ text: string; href: string }>> };

    const headers = [...headerRow.querySelectorAll('td,th')].map((c) =>
      ((c as HTMLElement).innerText || '').replace(/\s+/g, ' ').trim(),
    );
    const table = headerRow.closest('table');
    const trBody = [...(table?.querySelectorAll('tr') ?? [])].filter(
      (tr) => tr !== headerRow && tr.querySelectorAll('td').length >= headers.length - 1,
    );
    const body = trBody.map((tr) =>
      [...tr.querySelectorAll('td')].map((c) =>
        ((c as HTMLElement).innerText || '')
          .replace(/\u00a0/g, ' ')
          .replace(/[ \t]+/g, ' ')
          .trim(),
      ),
    );
    const links = trBody.map((tr) =>
      [...tr.querySelectorAll('a[href]')].map((a) => ({
        text: ((a as HTMLElement).innerText || '').trim(),
        href: (a as HTMLAnchorElement).href,
      })),
    );
    return { headers, body, links };
  });

  const idx = (...names: string[]): number =>
    rows.headers.findIndex((h) => names.some((n) => h.toLowerCase().includes(n)));

  const iYPM = idx('year');
  const iDesc = idx('description');
  const iMiles = idx('miles');
  const iGrade = idx('grade');
  const iStock = idx('stock');
  const iPrice = idx('price');
  const iDealer = idx('dealer');
  const iDist = idx('dist');

  const at = (cells: string[], i: number): string => (i >= 0 ? (cells[i] ?? '') : '');

  const listings: Listing[] = [];
  for (const [r, cells] of rows.body.entries()) {
    const priceCell = at(cells, iPrice);
    if (!/\d/.test(priceCell) && !at(cells, iStock)) continue;

    // "2004 / Engine Assembly / Volkswagen Touareg" stacked in one cell.
    const [year = '', part = '', ...modelParts] = at(cells, iYPM).split('\n').map((s) => s.trim());
    if (!/^\d{4}$/.test(year)) continue;

    // Dealer cell: name + location line, a phone number, and quote links.
    const dealerCell = at(cells, iDealer);
    const phone = (dealerCell.match(/1?[- ]?\(?\d{3}\)?[- .]\d{3}[- .]\d{4}/) ?? [''])[0].trim();
    const firstLine = dealerCell.split('\n')[0] ?? '';
    const locMatch = firstLine.match(/\b((?:USA|CAN|MEX)-[A-Z]{2}\s*\([^)]*\))/);
    const dealer = (locMatch ? firstLine.slice(0, locMatch.index) : firstLine)
      .replace(/Request_Quote/g, '')
      .trim();

    listings.push({
      searchedYear,
      year,
      model: modelParts.join(' ').trim(),
      part,
      description:
        at(cells, iDesc)
          .split('\n')
          .map((l) => l.trim())
          .filter((l) => l && !/estimated co2e/i.test(l))[0] ?? '',
      grade: at(cells, iGrade),
      price: priceCell.replace(/\s+/g, ' '),
      priceUsd: money(priceCell),
      mileage: at(cells, iMiles),
      distanceMiles: (() => {
        const d = at(cells, iDist).replace(/,/g, '').match(/\d+(?:\.\d+)?/);
        return d ? Number(d[0]) : distance(dealerCell);
      })(),
      dealer,
      location: locMatch ? locMatch[1].replace(/\s+/g, ' ') : '',
      phone,
      stockNumber: at(cells, iStock),
      ...rowLinks(rows.links[r] ?? []),
    });
  }
  return listings;
}

/**
 * Keeps the two row links that still work outside this browser session. The
 * quote form and map links carry the session's tk1..tk6 tokens and expire.
 */
function rowLinks(links: Array<{ text: string; href: string }>): Pick<Listing, 'photoUrl' | 'yardUrl'> {
  let photoUrl: string | undefined;
  let yardUrl: string | undefined;
  for (const { href } of links) {
    let u: URL;
    try {
      u = new URL(href);
    } catch {
      continue;
    }
    if (!/^https?:$/.test(u.protocol)) continue;
    if (/^image[a-z]*\.car-part\.com$/i.test(u.hostname)) photoUrl ??= href;
    else if (!/^(www|appcgi)\.car-part\.com$/i.test(u.hostname)) yardUrl ??= href;
  }
  return { photoUrl, yardUrl };
}

export async function search(opts: SearchOptions): Promise<SearchResult> {
  const log = opts.onLog ?? (() => {});
  const maxPages = opts.maxPages ?? 5;
  const delayMs = opts.delayMs ?? 1500;

  // Camoufox, not stock Chrome: Cloudflare fingerprints headless Chrome and serves
  // an interstitial that never resolves. Camoufox is a patched Firefox whose
  // fingerprint is generated at launch, and it clears the challenge.
  // The profile is always persistent so the clearance cookie survives between runs.
  const ctx: BrowserContext = await Camoufox({
    user_data_dir: opts.userDataDir ?? DEFAULT_PROFILE,
    headless: opts.headless ?? true,
    os: opts.fingerprintOs ?? 'windows',
    humanize: true,
    geoip: true,
    window: [1400, 1000],
  });

  await ctx.addInitScript(RADIO_LABEL_HELPER);
  const page = ctx.pages()[0] ?? (await ctx.newPage());

  const years = Array.isArray(opts.year) ? opts.year : [opts.year];
  const listings: Listing[] = [];
  const seen = new Set<string>();
  let interchangeOptions: string[] = [];
  let interchangeChosen: string | null = null;
  let pages = 0;

  try {
    for (const year of years) {
      log(`opening ${HOME}`);
      await page.goto(HOME, { waitUntil: 'domcontentloaded' });
      await settle(page, log);

      await page.selectOption('select[name=userDate]', String(year));
      await page.selectOption('select[name=userModel]', opts.model);
      await page.selectOption('select[name=userPart]', opts.part);
      if (opts.location) await page.selectOption('select[name=userLocation]', opts.location);
      if (opts.sort) await page.selectOption('select[name=userPreference]', opts.sort);
      if (opts.zip) await page.fill('input[name=userZip]', opts.zip);

      log(`searching ${year} ${opts.model} / ${opts.part}`);
      await Promise.all([
        page.waitForNavigation({ waitUntil: 'domcontentloaded' }),
        page.evaluate(() => (document.forms as any).cf.submit()),
      ]);
      await settle(page, log);

      // Interchange picker, shown when the part has several variants.
      if ((await page.locator('input[type=radio][name=dummyVar]').count()) > 0) {
        interchangeOptions = await page.evaluate(() =>
          [...document.querySelectorAll('input[type=radio][name=dummyVar]')]
            .filter((r) => (r as HTMLElement).style.display !== 'none')
            .map((r) => radioLabel(r as HTMLInputElement))
            .filter((t) => t.length > 0),
        );
        log(`interchange options: ${interchangeOptions.join(' / ')}`);

        const want = opts.interchange;
        if (want) {
          const pattern = want instanceof RegExp ? want.source : want;
          const flags = want instanceof RegExp ? want.flags : 'i';
          interchangeChosen = await page.evaluate(
            ({ pattern, flags }) => {
              const re = new RegExp(pattern, flags);
              for (const r of [...document.querySelectorAll('input[type=radio][name=dummyVar]')]) {
                const label = radioLabel(r as HTMLInputElement);
                if (label && re.test(label)) {
                  (r as HTMLInputElement).click();
                  return label;
                }
              }
              return null;
            },
            { pattern, flags },
          );
          if (!interchangeChosen) {
            throw new Error(
              `No interchange option matched ${String(want)} for ${year}. Options: ${interchangeOptions.join(' | ')}`,
            );
          }
          log(`chose interchange: ${interchangeChosen}`);
        }

        await Promise.all([
          page.waitForNavigation({ waitUntil: 'domcontentloaded' }),
          page.evaluate(() => (document.getElementById('MainForm') as HTMLFormElement).submit()),
        ]);
        await settle(page, log);
      }

      for (let pageNo = 1; pageNo <= maxPages; pageNo++) {
        pages++;
        if (process.env.CARPART_DUMP) {
          const { writeFileSync } = await import('node:fs');
          writeFileSync(`${process.env.CARPART_DUMP}/results-${year}-${pageNo}.html`, await page.content());
        }
        const batch = await parseListings(page, year);
        log(`${year} page ${pageNo}: ${batch.length} listings`);
        for (const l of batch) {
          const key = `${l.dealer}|${l.stockNumber}|${l.price}|${l.year}|${l.description}`;
          if (!seen.has(key)) {
            seen.add(key);
            listings.push(l);
          }
        }
        if (batch.length === 0) break;

        const nextHref = await page.evaluate((n) => {
          const links = [...document.querySelectorAll('a[href*="userPage="]')];
          const hit = links.find((a) =>
            new RegExp(`userPage=${n}(?:&|$)`).test((a as HTMLAnchorElement).href),
          );
          return hit ? (hit as HTMLAnchorElement).href : null;
        }, pageNo + 1);
        if (!nextHref) break;
        await page.waitForTimeout(delayMs);
        await page.goto(nextHref, { waitUntil: 'domcontentloaded' });
        await settle(page, log);
      }
      await page.waitForTimeout(delayMs);
    }

    return { interchangeOptions, interchangeChosen, pages, listings };
  } finally {
    await ctx.close();
  }
}
