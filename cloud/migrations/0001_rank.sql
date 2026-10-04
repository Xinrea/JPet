CREATE TABLE IF NOT EXISTS rankboard (
  uid TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  starcnt INTEGER NOT NULL,
  exp INTEGER NOT NULL,
  attr INTEGER NOT NULL,
  revision INTEGER NOT NULL,
  visible INTEGER NOT NULL DEFAULT 0,
  updated_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS rank_star ON rankboard(visible, starcnt DESC, uid ASC);
CREATE INDEX IF NOT EXISTS rank_exp ON rankboard(visible, exp DESC, uid ASC);
CREATE INDEX IF NOT EXISTS rank_attr ON rankboard(visible, attr DESC, uid ASC);
