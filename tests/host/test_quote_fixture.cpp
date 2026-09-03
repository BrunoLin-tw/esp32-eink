#include "quote_fixture.h"
#include <cassert>
#include <cstring>

int main() {
  qlogic::MarketBatch normal;
  fillQuoteFixture(&normal, "20260903", QuoteFixtureKind::Normal);
  assert(normal.rows[0].valid && normal.rows[6].valid);
  assert(strcmp(normal.date, "20260903") == 0);

  qlogic::MarketBatch untraded;
  fillQuoteFixture(&untraded, "20260903", QuoteFixtureKind::Untraded);
  assert(untraded.rows[5].valid);
  assert(untraded.rows[5].z == 0 && untraded.rows[5].y != 0);

  qlogic::MarketBatch partial;
  fillQuoteFixture(&partial, "20260903", QuoteFixtureKind::PartialFailure);
  assert(partial.rows[0].valid);
  assert(!partial.rows[6].valid);
  assert(partial.rows[6].z == 0 && partial.rows[6].y == 0);
  assert(partial.rows[6].t[0] == '\0');
}
