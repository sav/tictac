-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Savio Sena <savio.sena@gmail.com>
--
-- cheat.lua -- estimate the probability that a player received engine
-- assistance, from engine-correlation statistics over their games. Produces a
-- composite report: several independent indicators (move-match test, scaled
-- centipawn-loss test, intrinsic performance rating, hard-move detection,
-- clean-move rate, move-time regularity when clocks are present, and a
-- localized-assistance scan for the most engine-like contiguous stretch within
-- a game), each as a z-score against an honest-player model, folded into one
-- calibrated posterior. Needs a UCI engine.
--
--   tictac --file db.pgn --plugin \
--     "cheat.lua engine=stockfish player=^Doe depth=18 multipv=5 report=out.txt"
--
-- This is a screening tool, not proof. Read the caveats printed in the report.
-- Aggregates across the whole database via partial/reduce, so it runs at any
-- -j N and prints the same report; pair with --engines to cap engine processes.

local plugin = {
  meta = {
    name    = "cheat",
    version = "1.0.0",
    description = "Composite statistical engine-assistance estimator.",
    args = {
      engine      = { type = "string", default = "stockfish", help = "UCI engine path." },
      player      = { type = "string",                        help = "Lua pattern matched (case-insensitively) against White/Black. Repeatable. Absent: score every player." },
      depth       = { type = "number", default = 18,  help = "Search depth per position." },
      movetime    = { type = "number",                help = "Search time per position (ms); overrides depth when set." },
      multipv     = { type = "number", default = 5,   help = "Candidate moves examined per position (choice-set size)." },
      shallow     = { type = "number", default = 0,   help = "Shallow search depth for the deep-vs-shallow fingerprint (0 disables). Each position is also searched this shallow, and the player's agreement with the deep-only best move is measured." },
      ds_gap      = { type = "number", default = 20,  help = "Min centipawn gap (deep eval) between the deep best and the shallow best for a position to count as 'revealing' in the deep-vs-shallow test." },
      maia        = { type = "string",                help = "Path to an lc0 binary serving a Maia network. Enables the per-rating human-difficulty oracle." },
      maia_dir    = { type = "string",                help = "Directory holding maia-1100.pb.gz .. maia-1900.pb.gz; the band nearest the player's Elo is loaded." },
      maia_weights= { type = "string",                help = "Explicit Maia weights file, overriding maia_dir/band selection." },
      maia_backend= { type = "string", default = "cuda", help = "lc0 Backend UCI option; defaults to cuda. On an lc0 built without cuda, lc0 falls back to its own default backend, so this is safe on CPU-only builds too." },
      maia_nodes  = { type = "number", default = 1,   help = "lc0 nodes per policy read; 1 gives the raw human policy head." },
      exc_cp      = { type = "number", default = 50,  help = "Centipawn loss at which a move's 'excellence' fades to zero in the Maia test." },
      book        = { type = "number", default = 8,   help = "Skip moves at or before this full-move number (opening theory)." },
      maxply      = { type = "number",                help = "Stop analysing a game after this ply (0/absent: whole game)." },
      losscap     = { type = "number", default = 300, help = "Cap per-move centipawn loss at this value (lost-game noise)." },
      skip_decided= { type = "bool",   default = true, help = "Skip positions already decided (best-move win-prob outside [lo,hi])." },
      decided_wp  = { type = "number", default = 0.95, help = "Win-prob threshold for 'decided' when skip_decided." },
      assume_elo  = { type = "number", default = 2000, help = "Elo used for the honest model when a game has no *Elo header." },
      reference   = { type = "bool",   default = false, help = "Also analyse non-target players to calibrate the honest model from the field." },
      ref_min     = { type = "number", default = 400, help = "Minimum field moves before the data-driven calibration is trusted." },
      min_moves   = { type = "number", default = 40,  help = "Below this many analysed moves no verdict is issued." },
      seg_min     = { type = "number", default = 6,   help = "Shortest within-game stretch the localized-assistance scan considers." },
      seg_max     = { type = "number", default = 80,  help = "Longest stretch the scan considers (it hunts localized bursts, not whole games)." },
      seg_alpha   = { type = "number", default = 0.001, help = "Multiplicity-corrected p-value below which a stretch is reported as FLAGGED." },
      prior       = { type = "number", default = 0.02, help = "Prior probability of cheating (base rate) for the posterior." },
      beta        = { type = "number", default = 1.3, help = "Logistic slope mapping the combined z to the posterior. Calibrated against a labelled chess.com sample (see TODO) so the SUSPICIOUS band fires on the clearest cases at near-zero false positives." },
      cal_a       = { type = "number", default = 561, help = "Built-in Elo(tau) intercept: Elo = cal_a + cal_b*ln(1/tau)." },
      cal_b       = { type = "number", default = 521, help = "Built-in Elo(tau) slope." },
      disp_rate   = { type = "number", default = 0.06, help = "Between-player SD of the top-1/clean/hard-move match rate among honest peers (overdispersion). Caps the rate z-scores so they measure how extreme a player is, not how many moves were seen." },
      disp_acpl   = { type = "number", default = 20,   help = "Between-player SD of mean centipawn loss (cp) among honest peers; overdispersion term for the cp-loss z." },
      disp_exc    = { type = "number", default = 0.05, help = "Between-player SD of mean Maia excellence among honest peers; overdispersion term for the Maia z." },
      disp_elo    = { type = "number", default = 200,  help = "Irreducible SD (Elo) between a player's intrinsic and stated rating; floors the IPR z." },
      report      = { type = "string",                help = "Report path (default: stdout via ctx.out, else stderr log)." },
      csv         = { type = "string",                help = "Optional machine-readable one-row-per-player CSV." },
      annotate    = { type = "bool",   default = false, help = "Tag each analysed move with its centipawn loss as a PGN comment." },
      suspects    = { type = "number", default = 10, help = "List the N most suspicious individual moves per player in the report (0 disables)." },
      games       = { type = "number", default = 5,  help = "List the N most suspicious individual games, each scored in isolation, in the report (0 disables)." },
    },
  },
}

-- detail -------------------------------------------------------------------
-- Pure numerics and the honest-player choice model, kept TU-local to the
-- plugin. The statistics live here; process() only gathers per-move evidence
-- and finish() turns it into indicators.

local EPS_WP = 0.01   -- a move this close in win-prob to the best counts as "matching".
local MATE_CP = 2000  -- mate folded to a finite centipawn value for scaling.

local detail = {}

-- Error function (Abramowitz & Stegun 7.1.26) and the standard-normal CDF.
function detail.erf(x)
  local s = x < 0 and -1 or 1
  x = math.abs(x)
  local t = 1 / (1 + 0.3275911 * x)
  local y = 1 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t
             - 0.284496736) * t + 0.254829592) * t * math.exp(-x * x)
  return s * y
end

function detail.phi(z)             -- P(Z <= z)
  return 0.5 * (1 + detail.erf(z / math.sqrt(2)))
end

-- Inverse hyperbolic tangent (standard Lua has no math.atanh).
function detail.atanh(r)
  return 0.5 * math.log((1 + r) / (1 - r))
end

function detail.sf(z)              -- upper tail, one-sided p-value
  return 1 - detail.phi(z)
end

-- Inverse standard-normal CDF (Acklam's rational approximation). Maps a
-- probability back to a z-score; used to turn a multiplicity-corrected p-value
-- into a corrected z. Standard Lua has no probit, and phi above is not
-- invertible in closed form.
function detail.probit(p)
  if p <= 0 then return -math.huge end
  if p >= 1 then return math.huge end
  local a = { -3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
              1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00 }
  local b = { -5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
              6.680131188771972e+01, -1.328068155288572e+01 }
  local c = { -7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
              -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00 }
  local d = { 7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
              3.754408661907416e+00 }
  local plow = 0.02425
  if p < plow then
    local q = math.sqrt(-2 * math.log(p))
    return (((((c[1] * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) * q + c[6]) /
           ((((d[1] * q + d[2]) * q + d[3]) * q + d[4]) * q + 1)
  elseif p <= 1 - plow then
    local q = p - 0.5
    local r = q * q
    return (((((a[1] * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * r + a[6]) * q /
           (((((b[1] * r + b[2]) * r + b[3]) * r + b[4]) * r + b[5]) * r + 1)
  end
  local q = math.sqrt(-2 * math.log(1 - p))
  return -(((((c[1] * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) * q + c[6]) /
          ((((d[1] * q + d[2]) * q + d[3]) * q + d[4]) * q + 1)
end

-- Classic logistic centipawn -> win-probability (mover's perspective).
function detail.winprob(cp)
  return 1 / (1 + 10 ^ (-cp / 400))
end

-- Fold a (score, mate) evaluation, side-to-move relative, into a capped cp.
function detail.eval_cp(score, mate)
  if mate ~= nil then
    if mate > 0 then return MATE_CP end
    if mate < 0 then return -MATE_CP end
    return 0
  end
  if score == nil then return 0 end
  if score > MATE_CP then return MATE_CP end
  if score < -MATE_CP then return -MATE_CP end
  return score
end

-- log(sum_i exp(v_i)) computed stably.
function detail.logsumexp(v)
  local m = -math.huge
  for _, x in ipairs(v) do if x > m then m = x end end
  if m == -math.huge then return -math.huge end
  local s = 0
  for _, x in ipairs(v) do s = s + math.exp(x - m) end
  return m + math.log(s)
end

-- Boltzmann (multinomial-logit) choice model. A player of "temperature" tau
-- selects an option with win-prob loss L with probability proportional to
-- exp(-L/tau): tau -> 0 is a perfect player, larger tau a weaker one. Returns
-- the probability of each option in `losses`.
function detail.choice_probs(losses, tau)
  local v = {}
  for i, L in ipairs(losses) do v[i] = -L / tau end
  local z = detail.logsumexp(v)
  local p = {}
  for i = 1, #v do p[i] = math.exp(v[i] - z) end
  return p
end

-- Log-likelihood of a set of moves under the model at `tau`. Each move is
-- { lwp = {option win-prob losses}, chosen = picked-option index }.
function detail.loglik(moves, tau)
  local ll = 0
  for _, m in ipairs(moves) do
    local v = {}
    for i, L in ipairs(m.lwp) do v[i] = -L / tau end
    ll = ll + v[m.chosen] - detail.logsumexp(v)
  end
  return ll
end

-- Maximum-likelihood tau by golden-section search in log-space (tau is a
-- scale, so ln tau is the natural coordinate). Returns tau_hat and the SE of
-- tau_hat from the observed Fisher information (-1/LL'').
function detail.fit_tau(moves, lo, hi)
  local gr = (math.sqrt(5) - 1) / 2
  local a, b = math.log(lo), math.log(hi)
  local f = function(u) return detail.loglik(moves, math.exp(u)) end
  local c = b - gr * (b - a)
  local d = a + gr * (b - a)
  local fc, fd = f(c), f(d)
  for _ = 1, 80 do
    if fc < fd then
      a, c, fc = c, d, fd
      d = a + gr * (b - a); fd = f(d)
    else
      b, d, fd = d, c, fc
      c = b - gr * (b - a); fc = f(c)
    end
    if math.abs(b - a) < 1e-5 then break end
  end
  local tau = math.exp((a + b) / 2)

  -- Observed information from a numerical second derivative of LL(tau).
  local h = math.max(tau * 0.05, 1e-4)
  local l0 = detail.loglik(moves, tau)
  local lp = detail.loglik(moves, tau + h)
  local lm = detail.loglik(moves, math.max(tau - h, lo * 0.5))
  local d2 = (lp + lm - 2 * l0) / (h * h)
  local se = (d2 < 0) and math.sqrt(-1 / d2) or nil
  return tau, se, l0
end

-- Pearson correlation of two equal-length arrays, or nil if degenerate.
function detail.pearson(xs, ys)
  local n = #xs
  if n < 3 then return nil end
  local mx, my = 0, 0
  for i = 1, n do mx = mx + xs[i]; my = my + ys[i] end
  mx, my = mx / n, my / n
  local sxy, sxx, syy = 0, 0, 0
  for i = 1, n do
    local dx, dy = xs[i] - mx, ys[i] - my
    sxy = sxy + dx * dy; sxx = sxx + dx * dx; syy = syy + dy * dy
  end
  if sxx <= 0 or syy <= 0 then return nil end
  return sxy / math.sqrt(sxx * syy)
end

return (function()

-- Built-in honest calibration: Elo = a + b*ln(1/tau), inverted to recover the
-- temperature an honest player of a given Elo is expected to play at. Used when
-- no field reference is available.
local function builtin_tau(elo, a, b)
  return math.exp(-(elo - a) / b)
end
local function builtin_elo(tau, a, b)
  return a + b * math.log(1 / tau)
end

-- Pick the Maia weights file for a player's Elo. Maia ships one network per
-- rating band (maia-1100 .. maia-1900); round to the nearest hundred, clamp to
-- the trained range, and name the file inside maia_dir. An explicit maia_weights
-- overrides this.
local function maia_band(elo)
  local b = math.floor(elo / 100 + 0.5) * 100
  return math.max(1100, math.min(1900, b))
end
local function maia_weights_for(args, elo)
  if args.maia_weights then return args.maia_weights, maia_band(elo) end
  local band = maia_band(elo)
  return string.format("%s/maia-%d.pb.gz", args.maia_dir, band), band
end

-- Excellence of a move given its centipawn loss: 1 for a best move, fading to 0
-- by exc_cp. The signal we test is whether a player plays excellent moves more
-- often than a human of their rating band (per Maia) would.
local function excellence(lcp, exc_cp)
  local e = (exc_cp - lcp) / exc_cp
  if e < 0 then return 0 end
  if e > 1 then return 1 end
  return e
end

-- Parse "h:mm:ss", "mm:ss" or a bare seconds count out of a [%clk ...] or
-- [%emt ...] move comment. Returns seconds or nil.
local function move_seconds(mv)
  local c = mv:comment()
  if not c then return nil end
  local hms = c:match("%%clk%s+([%d:%.]+)") or c:match("%%emt%s+([%d:%.]+)")
  if not hms then return nil end
  local parts = {}
  for p in hms:gmatch("[^:]+") do parts[#parts + 1] = tonumber(p) end
  if #parts == 3 then return parts[1] * 3600 + parts[2] * 60 + parts[3] end
  if #parts == 2 then return parts[1] * 60 + parts[2] end
  if #parts == 1 then return parts[1] end
  return nil
end

-- The game's URL for the report. Lichess puts it in [Site]; chess.com puts the
-- literal "Chess.com" in [Site] and the real URL in [Link]. Prefer whichever
-- header is an http(s) URL, and give nothing when neither is.
local function game_url(game)
  local site = game:header("Site")
  local link = game:header("Link")
  if link and link:match("^https?://") then return link end
  if site and site:match("^https?://") then return site end
  return nil
end

-- Build the per-move evidence record from an engine result at board_before and
-- the move actually played. Returns { lwp, lcp, chosen } or nil when the
-- position is unusable (no candidates, or decided and skipped).
local function build_record(sf, node, args)
  local depth    = args.depth
  local movetime = args.movetime
  local K        = args.multipv
  local losscap  = args.losscap

  local limits = movetime and { movetime = movetime, multipv = K }
                           or { depth = depth, multipv = K }
  local r = sf:analyse(node.board_before, limits)

  -- Normalise candidate lines to a { uci, cp } list, mover-relative.
  local cands = {}
  if r.lines and #r.lines > 0 then
    for _, ln in ipairs(r.lines) do
      local uci = ln.pv and ln.pv[1]
      if uci then cands[#cands + 1] = { uci = uci, cp = detail.eval_cp(ln.score, ln.mate) } end
    end
  elseif r.bestmove then
    cands[1] = { uci = r.bestmove, cp = detail.eval_cp(r.score, r.mate) }
  end
  if #cands == 0 then return nil end

  local best_cp = cands[1].cp
  local best_wp = detail.winprob(best_cp)
  if args.skip_decided and (best_wp >= args.decided_wp or best_wp <= 1 - args.decided_wp) then
    return nil
  end

  -- Locate the played move among the candidates; if it is outside the top-K,
  -- evaluate the resulting position once to price it, and append it.
  local played = node.move:uci()
  local ncand = #cands   -- genuine top-K before any appended played move
  local chosen
  for i, c in ipairs(cands) do
    if c.uci == played then chosen = i; break end
  end
  if not chosen then
    local lim2 = movetime and { movetime = movetime } or { depth = depth }
    local ra = sf:analyse(node.board_after, lim2)
    local played_cp = -detail.eval_cp(ra.score, ra.mate)   -- opponent-relative -> mover-relative
    cands[#cands + 1] = { uci = played, cp = played_cp }
    chosen = #cands
  end

  local lwp, lcp, ucis = {}, {}, {}
  for i, c in ipairs(cands) do
    local l_wp = best_wp - detail.winprob(c.cp)
    if l_wp < 0 then l_wp = 0 end
    local l_cp = best_cp - c.cp
    if l_cp < 0 then l_cp = 0 end
    if l_cp > losscap then l_cp = losscap end
    lwp[i], lcp[i], ucis[i] = l_wp, l_cp, c.uci
  end
  local rec = { lwp = lwp, lcp = lcp, ucis = ucis, chosen = chosen }

  -- Deep-vs-shallow fingerprint: does the player follow the move that only deep
  -- search prefers? Humans play the shallow-obvious move; an engine user plays
  -- the deep-only best. Compare the deep best (cands[1]) with a shallow search's
  -- best: a position is "revealing" when they differ by a real margin, and we
  -- record whether the player chose the deep best there.
  if args.shallow and args.shallow > 0 then
    local rs = sf:analyse(node.board_before, { depth = args.shallow, multipv = 1 })
    local sbest = (rs.lines and rs.lines[1] and rs.lines[1].pv and rs.lines[1].pv[1]) or rs.bestmove
    local dbest = cands[1].uci
    if sbest then
      local differ = sbest ~= dbest
      local gap = args.ds_gap
      if differ then
        local scp
        for _, c in ipairs(cands) do if c.uci == sbest then scp = c.cp; break end end
        gap = scp and (best_cp - scp) or args.ds_gap   -- outside deep top-K => clearly worse
      end
      rec.ds = {
        reveal = (differ and gap >= args.ds_gap) and 1 or 0,
        pdeep  = (played == dbest) and 1 or 0,
        seqd   = (not differ) and 1 or 0,
      }
    end
  end
  return rec
end

-- Fit the honest model from the field, bucketed by Elo. Returns a function
-- elo -> tau interpolating the data-driven buckets, or nil if too sparse.
local function field_calibration(field, ref_min)
  if #field < ref_min then return nil end
  local buckets = {}   -- key: Elo/100 floored
  for _, m in ipairs(field) do
    local e = m.elo or 2000
    local k = math.floor(e / 100) * 100
    buckets[k] = buckets[k] or {}
    local b = buckets[k]
    b[#b + 1] = m
  end
  local keys = {}
  for k, ms in pairs(buckets) do
    if #ms >= 60 then keys[#keys + 1] = k end
  end
  if #keys < 2 then return nil end
  table.sort(keys)
  local pts = {}
  for _, k in ipairs(keys) do
    local tau = detail.fit_tau(buckets[k], 1e-3, 3)
    pts[#pts + 1] = { elo = k + 50, tau = tau }
  end
  return function(elo)
    if elo <= pts[1].elo then return pts[1].tau end
    if elo >= pts[#pts].elo then return pts[#pts].tau end
    for i = 2, #pts do
      if elo <= pts[i].elo then
        local a, b = pts[i - 1], pts[i]
        local t = (elo - a.elo) / (b.elo - a.elo)
        return a.tau + t * (b.tau - a.tau)   -- linear in tau; monotone enough locally
      end
    end
    return pts[#pts].tau
  end
end

-- Localized-assistance scan. A player who turns the engine on for only part of
-- a game is invisible to whole-game averages: the honest stretch dilutes the
-- assisted one. So scan every contiguous within-game window of at least `lmin`
-- moves (and at most `lmax`, since the target is a localized burst, not the
-- whole game), standardize its observed matches against the same per-move
-- honest expectation, and keep the single most engine-like window. `mi` is the
-- per-move match indicator (0/1), `pe` the honest match probability, `gid` the
-- game each move belongs to (windows never cross a game boundary). Returns the
-- best window plus the number of windows examined, so finish() can correct the
-- window p-value for having looked at that many. nil when data is too thin.
local function scan_segment(mi, pe, gid, lmin, lmax)
  local n = #mi
  if n < lmin then return nil end
  local O, E, V = { [0] = 0 }, { [0] = 0 }, { [0] = 0 }
  for k = 1, n do
    O[k] = O[k - 1] + mi[k]
    E[k] = E[k - 1] + pe[k]
    V[k] = V[k - 1] + pe[k] * (1 - pe[k])
  end
  local best, nwin = nil, 0
  for i = 1, n - lmin + 1 do
    local jmax = math.min(n, i + lmax - 1)
    for j = i + lmin - 1, jmax do
      if gid[j] ~= gid[i] then break end   -- a window stays inside one game
      local v = V[j] - V[i - 1]
      if v > 0 then
        nwin = nwin + 1
        local o = O[j] - O[i - 1]
        local e = E[j] - E[i - 1]
        local zz = (o - e) / math.sqrt(v)
        if not best or zz > best.z then
          best = { i = i, j = j, obs = o, exp = e, z = zz, len = j - i + 1 }
        end
      end
    end
  end
  if not best then return nil end
  best.nwin = nwin
  return best
end

-- Compose the indicators for one player into z-scores against the honest model.
local function evaluate(pl, honest_tau, args)
  local moves = pl.moves
  local n = #moves
  local out = { n = n }

  -- Observed descriptive stats.
  local obs_match, obs_cp, obs_clean = 0, 0, 0
  for _, m in ipairs(moves) do
    if m.lwp[m.chosen] <= EPS_WP then obs_match = obs_match + 1 end
    if m.lcp[m.chosen] <= 10 then obs_clean = obs_clean + 1 end
    obs_cp = obs_cp + m.lcp[m.chosen]
  end
  out.t1 = obs_match / n
  out.acpl = obs_cp / n
  out.clean = obs_clean / n

  -- Per-move honest expectations under the Boltzmann model at honest_tau.
  local E_match, V_match = 0, 0
  local E_cp, V_cp = 0, 0
  local E_clean, V_clean = 0, 0
  local E_hw, V_hw, O_hw, S_hw = 0, 0, 0, 0   -- hard-move weighted match (S_hw = sum of weights)
  local times, diffs = {}, {}
  local mi, pe, gid, mvno = {}, {}, {}, {}   -- per-move series for the segment scan
  local gmeta = {}                            -- gid -> { gtag, gurl, side } for the per-game report
  local susp = {}                             -- per-move suspicion, ranked for the report
  for _, m in ipairs(moves) do
    local p = detail.choice_probs(m.lwp, honest_tau)
    local pm, ec, ec2, pc = 0, 0, 0, 0
    for i = 1, #p do
      if m.lwp[i] <= EPS_WP then pm = pm + p[i] end
      if m.lcp[i] <= 10 then pc = pc + p[i] end
      ec = ec + p[i] * m.lcp[i]
      ec2 = ec2 + p[i] * m.lcp[i] * m.lcp[i]
    end
    local w = 1 - pm                        -- position difficulty
    local hit = m.lwp[m.chosen] <= EPS_WP
    E_match = E_match + pm;        V_match = V_match + pm * (1 - pm)
    E_clean = E_clean + pc;        V_clean = V_clean + pc * (1 - pc)
    E_cp    = E_cp + ec;           V_cp    = V_cp + (ec2 - ec * ec)
    E_hw    = E_hw + pm * w;       V_hw    = V_hw + w * w * pm * (1 - pm);  S_hw = S_hw + w
    if hit then O_hw = O_hw + w end
    if m.time then times[#times + 1] = m.time; diffs[#diffs + 1] = w end
    local k = #mi + 1
    mi[k], pe[k] = hit and 1 or 0, pm
    gid[k], mvno[k] = m.gi or 1, m.movno or 0
    if not gmeta[gid[k]] then gmeta[gid[k]] = { gtag = m.gtag, gurl = m.gurl, side = m.side } end
    -- Per-move suspicion: the hard-move match contribution w*(hit - pm) -- high
    -- when the player found the best move in a position an honest peer rarely
    -- solves. Replaced by the Maia excellence excess below when a prior exists.
    susp[k] = { m = m, pm = pm, score = w * ((hit and 1 or 0) - pm) }
  end

  -- Localized-assistance scan: the strongest contiguous within-game stretch,
  -- then a Bonferroni correction over the windows examined. Oriented one-sided
  -- (an excess of matches is the suspicious direction); only a positive
  -- corrected z feeds the fusion, so the absence of a hot streak is not taken
  -- as evidence of innocence beyond what the global stats already say.
  local seg = scan_segment(mi, pe, gid, args.seg_min, args.seg_max)
  if seg then
    seg.m0, seg.m1 = mvno[seg.i], mvno[seg.j]
    seg.rate, seg.exprate = seg.obs / seg.len, seg.exp / seg.len
    seg.pcorr = math.min(1, detail.sf(seg.z) * seg.nwin)
    seg.zc = math.max(-8, math.min(8, detail.probit(1 - seg.pcorr)))
    out.seg = seg
    if seg.zc > 0 then out.z_seg = seg.zc end
  end

  -- Per-game localized scan. The global 'best stretch' above dilutes an isolated
  -- cheated game across the whole history -- its Bonferroni runs over every window
  -- of every game, so one engine-like game in a thousand never survives. Here each
  -- game is scored on its own: find its hottest stretch, correct for the windows
  -- inside that game, then for the number of games the player has. A single
  -- engine-like game then stands out even while the whole-player verdict stays low.
  if args.games > 0 then
    local bygame = {}
    for k = 1, #mi do
      local b = bygame[gid[k]]
      if not b then b = { mi = {}, pe = {}, mvno = {} }; bygame[gid[k]] = b end
      local j = #b.mi + 1
      b.mi[j], b.pe[j], b.mvno[j] = mi[k], pe[k], mvno[k]
    end
    local gkeys = {}
    for g in pairs(bygame) do gkeys[#gkeys + 1] = g end
    local ngames = #gkeys
    local glist = {}
    for _, g in ipairs(gkeys) do
      local b = bygame[g]
      local gn = #b.mi
      local ones = {}
      for j = 1, gn do ones[j] = 1 end
      local gs = scan_segment(b.mi, b.pe, ones, args.seg_min, args.seg_max)
      if gs then
        local O, E = 0, 0
        for j = 1, gn do O = O + b.mi[j]; E = E + b.pe[j] end
        local pin = math.min(1, detail.sf(gs.z) * gs.nwin)   -- windows inside this game
        local pgame = math.min(1, pin * ngames)              -- games examined
        local zc = math.max(-8, math.min(8, detail.probit(1 - pgame)))
        local meta = gmeta[g] or {}
        glist[#glist + 1] = {
          zc = zc, pgame = pgame, flagged = pgame <= args.seg_alpha, n = gn,
          m0 = b.mvno[gs.i], m1 = b.mvno[gs.j], len = gs.len,
          rate = gs.obs / gs.len, exprate = gs.exp / gs.len,
          gmatch = O / gn, gexp = E / gn,
          gtag = meta.gtag, gurl = meta.gurl, side = meta.side, movno = b.mvno[gs.i],
        }
      end
    end
    -- content-based total order so -j1 and -j N list the same games byte-for-byte
    table.sort(glist, function(x, y)
      if x.zc ~= y.zc then return x.zc > y.zc end
      if (x.m0 or 0) ~= (y.m0 or 0) then return (x.m0 or 0) < (y.m0 or 0) end
      if (x.gtag or "") ~= (y.gtag or "") then return (x.gtag or "") < (y.gtag or "") end
      return (x.gurl or "") < (y.gurl or "")
    end)
    local top = {}
    for _, g in ipairs(glist) do
      if g.zc <= 0 or #top >= args.games then break end
      top[#top + 1] = g
    end
    out.topgames = top
    out.ngames_scanned = ngames
  end

  -- Maia difficulty oracle: does the player play excellent moves more often than
  -- a human of their own rating band does? Maia's per-move policy supplies the
  -- honest null directly -- E[excellence] under the moves a human actually
  -- weights -- so forcing moves (which Maia also finds) count for little on
  -- their own, and only genuinely hard moves that were found push the z up.
  local O_e, E_e, V_e, maia_n, ph_sum = 0, 0, 0, 0, 0
  local hard = nil
  local mj = 0
  for _, m in ipairs(moves) do
    mj = mj + 1
    if m.priors then
      maia_n = maia_n + 1
      local ee, ee2, sum_pr = 0, 0, 0
      for i = 1, #m.ucis do
        local pr = m.priors[m.ucis[i]] or 0
        local e = excellence(m.lcp[i], args.exc_cp)
        ee = ee + pr * e
        ee2 = ee2 + pr * e * e
        sum_pr = sum_pr + pr
      end
      -- Moves outside the analysed top-K carry the leftover human-probability
      -- mass; they are all worse than the K-th best, so bound their excellence
      -- by its -- an upper bound that keeps the honest baseline conservative
      -- (it never understates how well a human of this band plays).
      local residual = 1 - sum_pr
      if residual > 0 then
        local eb = excellence(m.lcp[m.ncand or #m.ucis], args.exc_cp)
        ee = ee + residual * eb
        ee2 = ee2 + residual * eb * eb
      end
      local e_played = excellence(m.lcp[m.chosen], args.exc_cp)
      local p_played = m.priors[m.ucis[m.chosen]] or 0
      O_e = O_e + e_played
      E_e = E_e + ee
      V_e = V_e + math.max(0, ee2 - ee * ee)
      ph_sum = ph_sum + p_played
      -- With a human prior in hand, rank this move by excellence excess instead:
      -- how much more excellent the played move is than Maia expects of a peer.
      susp[mj].score = e_played - ee
      susp[mj].maia = true
      susp[mj].pplayed = p_played
      -- the hardest excellent move actually found: lowest human-probability
      -- among near-best moves played
      if e_played >= 0.8 and (not hard or p_played < hard.p) then
        hard = { mv = m.movno or 0, p = p_played }
      end
    end
  end
  if maia_n > 0 then
    out.maia_n = maia_n
    out.maia_band = pl.maia_band
    out.maia_mean_ph = ph_sum / maia_n
    out.maia_hard = hard
    local V_e_eff = V_e + (maia_n * args.disp_exc) ^ 2
    if V_e_eff > 0 then out.z_maia = (O_e - E_e) / math.sqrt(V_e_eff) end
  end

  -- Standardize against an OVERDISPERSED null: binomial (within-player) variance
  -- plus a between-player term (weight*SD)^2. Honest players of one rating band
  -- vary among themselves, and the honest model is only an approximation, so the
  -- real null spread is wider than binomial. Without this term a constant per-move
  -- bias b makes z grow like sqrt(n), and every player with enough moves trips the
  -- threshold; with it z converges to b/SD, a genuine effect size.
  -- Deep-vs-shallow agreement: among revealing positions (only deep search
  -- prefers a different move), how often the player played the deep-only best,
  -- contrasted with agreement on the obvious positions (shallow == deep).
  local ds_reveal, ds_deep, ds_obv, ds_obv_deep = 0, 0, 0, 0
  for _, m in ipairs(moves) do
    if m.ds then
      if m.ds.reveal == 1 then
        ds_reveal = ds_reveal + 1
        if m.ds.pdeep == 1 then ds_deep = ds_deep + 1 end
      elseif m.ds.seqd == 1 then
        ds_obv = ds_obv + 1
        if m.ds.pdeep == 1 then ds_obv_deep = ds_obv_deep + 1 end
      end
    end
  end
  if ds_reveal > 0 then
    out.ds_n = ds_reveal
    out.ds_rateB = ds_deep / ds_reveal
    if ds_obv > 0 then out.ds_rateA = ds_obv_deep / ds_obv end
  end

  local function z(obs, e, v, weight, sd)
    v = v + (weight * sd) ^ 2
    return v > 0 and (obs - e) / math.sqrt(v) or 0
  end
  local dr = args.disp_rate
  out.exp_match = E_match / n
  out.exp_acpl  = E_cp / n
  out.z_match = z(obs_match, E_match, V_match, n, dr)       -- more top matches than honest
  out.z_acpl  = z(E_cp, obs_cp, V_cp, n, args.disp_acpl)    -- less loss than honest (sign flipped)
  out.z_clean = z(obs_clean, E_clean, V_clean, n, dr)       -- more near-perfect moves
  out.z_cplx  = z(O_hw, E_hw, V_hw, S_hw, dr)               -- matches concentrated on hard moves

  -- Move-time regularity: honest players spend longer on harder positions, so
  -- a non-positive time/difficulty correlation is the suspicious direction.
  out.z_time = nil
  out.n_timed = #times
  if #times >= 15 then
    local r = detail.pearson(times, diffs)
    if r then
      r = math.max(-0.999, math.min(0.999, r))
      out.corr_time = r
      out.z_time = -detail.atanh(r) * math.sqrt(#times - 3)   -- Fisher z, flipped
    end
  end

  -- Intrinsic performance rating: fit the player's own temperature and map it
  -- to an implied Elo; compare against the rating they claim.
  local tau, se = detail.fit_tau(moves, 1e-3, 3)
  out.tau = tau
  out.ipr = math.max(500, math.min(3600, builtin_elo(tau, args.cal_a, args.cal_b)))
  if pl.elo_n > 0 and se then
    local stated = pl.elo_sum / pl.elo_n
    out.stated_elo = stated
    -- Delta method on Elo(tau), floored by the irreducible between-player spread
    -- (disp_elo): a player's intrinsic and stated ratings legitimately differ by a
    -- couple hundred Elo, so without this floor the IPR z also grows like sqrt(n).
    local se_elo = math.sqrt((args.cal_b * se / tau) ^ 2 + args.disp_elo ^ 2)
    out.z_ipr = se_elo > 0 and (out.ipr - stated) / se_elo or 0
  end

  -- The N most engine-like individual moves, strongest first. Only moves whose
  -- suspicion is positive (more engine-like than the honest baseline) qualify,
  -- so a clean player can yield fewer than N or none at all.
  if args.suspects > 0 and #susp > 0 then
    -- A total order by move content, not by processing order: worker order (and
    -- the per-worker game renumbering) must not change which ties are listed, so
    -- the report stays byte-identical between -j1 and -j N.
    local function uci_of(m) return m.ucis and m.ucis[m.chosen] or "" end
    table.sort(susp, function(x, y)
      if x.score ~= y.score then return x.score > y.score end
      if x.pm ~= y.pm then return x.pm < y.pm end
      local mx, my = x.m, y.m
      if (mx.movno or 0) ~= (my.movno or 0) then return (mx.movno or 0) < (my.movno or 0) end
      if (mx.side or "") ~= (my.side or "") then return (mx.side or "") < (my.side or "") end
      if (mx.gtag or "") ~= (my.gtag or "") then return (mx.gtag or "") < (my.gtag or "") end
      return uci_of(mx) < uci_of(my)
    end)
    local list = {}
    for _, su in ipairs(susp) do
      if su.score <= 0 or #list >= args.suspects then break end
      local m = su.m
      list[#list + 1] = {
        movno = m.movno or 0, san = m.san, uci = uci_of(m),
        side = m.side, gtag = m.gtag, gurl = m.gurl, lcp = m.lcp[m.chosen],
        pm = su.pm, maia = su.maia, pplayed = su.pplayed,
      }
    end
    out.suspects = list
  end
  return out
end

-- Fold the available indicator z-scores into one combined z (Stouffer, with a
-- redundancy deflation since the indicators are correlated), then a calibrated
-- posterior. Small samples are pulled back toward the prior.
local function aggregate(out, args)
  -- When the Maia oracle is present it is the principled difficulty signal, so
  -- it leads and the crude hard-move proxy (z_cplx) steps aside to avoid
  -- double-counting; without Maia, z_cplx carries the difficulty load.
  local comps = {
    { z = out.z_maia,  w = 1.3 },
    { z = out.z_match, w = 1.0 },
    { z = out.z_acpl,  w = 1.0 },
    { z = out.z_ipr,   w = 1.0 },
    { z = out.z_seg,   w = 0.9 },
    { z = out.z_cplx,  w = out.z_maia and 0.0 or 0.6 },
    { z = out.z_clean, w = 0.5 },
    { z = out.z_time,  w = 0.6 },
  }
  local num, den = 0, 0
  local k = 0
  for _, c in ipairs(comps) do
    if c.z ~= nil then
      num = num + c.w * c.z
      den = den + c.w * c.w
      k = k + 1
    end
  end
  if den == 0 then out.Z = 0; out.pvalue = 0.5; out.posterior = args.prior; return end
  -- Deflate for correlation among indicators: between fully independent
  -- (sqrt(den)) and fully redundant (sum of weights). Split the difference.
  local indep = math.sqrt(den)
  local redund = 0
  for _, c in ipairs(comps) do if c.z ~= nil then redund = redund + c.w end end
  local scale = 0.5 * (indep + redund)
  out.Z = num / scale
  out.pvalue = detail.sf(out.Z)

  -- Posterior: logit(P) = logit(prior) + beta*Z, with beta shrunk toward 0 when
  -- the sample is thin, so weak evidence stays near the base rate.
  local shrink = math.min(1, out.n / (2 * args.min_moves))
  local lp = math.log(args.prior / (1 - args.prior)) + args.beta * shrink * out.Z
  out.posterior = 1 / (1 + math.exp(-lp))
end

local function verdict(out, min_moves)
  if out.n < min_moves then return "INSUFFICIENT DATA" end
  local p = out.posterior
  if p >= 0.85 then return "HIGHLY SUSPICIOUS" end
  if p >= 0.50 then return "SUSPICIOUS" end
  if p >= 0.15 then return "INCONCLUSIVE" end
  return "NO SIGNAL"
end

-- plugin -------------------------------------------------------------------

function plugin.init(ctx)
  local s = ctx.scope
  s.sf = ctx.engine(ctx.args:get("engine", "stockfish"))
  s.players = {}      -- name -> { moves, games, wins, elo_sum, elo_n }
  s.field = {}        -- pooled non-target moves for calibration
  s.nmoves = 0

  local function each(v)
    if v == nil then return {} end
    return type(v) == "table" and v or { v }
  end
  s.patterns = each(ctx.args:get("player"))

  s.args = {
    depth        = ctx.args:number("depth", 18),
    movetime     = ctx.args:number("movetime"),
    multipv      = math.max(2, ctx.args:number("multipv", 5)),
    shallow      = math.max(0, ctx.args:number("shallow", 0)),
    ds_gap       = math.max(0, ctx.args:number("ds_gap", 20)),
    book         = ctx.args:number("book", 8),
    maxply       = ctx.args:number("maxply"),
    losscap      = ctx.args:number("losscap", 300),
    skip_decided = ctx.args:bool("skip_decided", true) ~= false,
    decided_wp   = ctx.args:number("decided_wp", 0.95),
    assume_elo   = ctx.args:number("assume_elo", 2000),
    reference    = ctx.args:bool("reference", false) == true,
    ref_min      = ctx.args:number("ref_min", 400),
    min_moves    = ctx.args:number("min_moves", 40),
    prior        = ctx.args:number("prior", 0.02),
    beta         = ctx.args:number("beta", 1.3),
    cal_a        = ctx.args:number("cal_a", 561),
    cal_b        = ctx.args:number("cal_b", 521),
    disp_rate    = math.max(0, ctx.args:number("disp_rate", 0.06)),
    disp_acpl    = math.max(0, ctx.args:number("disp_acpl", 20)),
    disp_exc     = math.max(0, ctx.args:number("disp_exc", 0.05)),
    disp_elo     = math.max(0, ctx.args:number("disp_elo", 200)),
    seg_min      = math.max(3, ctx.args:number("seg_min", 6)),
    seg_max      = math.max(ctx.args:number("seg_min", 6), ctx.args:number("seg_max", 80)),
    seg_alpha    = ctx.args:number("seg_alpha", 0.001),
    maia         = ctx.args:get("maia"),
    maia_dir     = ctx.args:get("maia_dir"),
    maia_weights = ctx.args:get("maia_weights"),
    maia_backend = ctx.args:get("maia_backend", "cuda"),
    maia_nodes   = math.max(1, ctx.args:number("maia_nodes", 1)),
    exc_cp       = math.max(1, ctx.args:number("exc_cp", 50)),
    annotate     = ctx.args:bool("annotate", false) == true,
    suspects     = math.max(0, ctx.args:number("suspects", 10)),
    games        = math.max(0, ctx.args:number("games", 5)),
  }

  s.maia = nil   -- lazily created on first target move, banded to its Elo
end

local function target_of(name, patterns)
  if name == nil or name == "" then return false end
  if #patterns == 0 then return true end
  local low = name:lower()
  for _, p in ipairs(patterns) do
    if low:find(p:lower()) then return true end
  end
  return false
end

function plugin.process(input, ctx)
  local s = ctx.scope
  local a = s.args
  local game = input.game
  local white = game:header("White")
  local black = game:header("Black")
  local welo = tonumber(game:header("WhiteElo") or "")
  local belo = tonumber(game:header("BlackElo") or "")

  local wt = target_of(white, s.patterns)
  local bt = target_of(black, s.patterns)
  if not (wt or bt or a.reference) then return input end

  local function get_player(name)
    local p = s.players[name]
    if not p then
      p = { moves = {}, games = 0, wins = 0, elo_sum = 0, elo_n = 0 }
      s.players[name] = p
    end
    return p
  end
  if wt then get_player(white).games = s.players[white].games + 1 end
  if bt then get_player(black).games = s.players[black].games + 1 end

  for node in game:positions() do
    if a.maxply and node.ply > a.maxply then break end
    local before = node.board_before
    local fmn = before:fullmoveNumber()
    if fmn > a.book and #before:legalMoves() > 1 then
      local mover = before:sideToMove()
      local is_white = mover == "white"
      local name = is_white and white or black
      local elo  = is_white and welo or belo
      local is_target = (is_white and wt) or ((not is_white) and bt)
      if is_target or a.reference then
        local ok, rec = pcall(build_record, s.sf, node, a)
        if ok and rec then
          rec.elo = elo or a.assume_elo
          rec.time = move_seconds(node.move)
          rec.movno = fmn
          if is_target then
            local p = get_player(name)
            rec.gi = p.games
            rec.san = node.move:san()
            rec.side = is_white and "White" or "Black"
            rec.gtag = (white or "?") .. " vs " .. (black or "?")
            rec.gurl = game_url(game)
            if a.maia then
              if not s.maia then
                local wf, band = maia_weights_for(a, rec.elo)
                -- Resolve the backend up front: cuda is the default, but fall back
                -- to lc0's own default when this build lacks it, rather than wedging.
                local opts = { WeightsFile = wf }
                if a.maia_backend then opts.Backend = a.maia_backend end
                local ok2, eng = pcall(ctx.engine, a.maia, opts)
                if ok2 then
                  s.maia, s.maia_band = eng, band
                else
                  ctx.log.warn("maia engine '%s' failed; disabling oracle: %s", a.maia, tostring(eng))
                  a.maia = nil
                end
              end
              if s.maia then
                local okp, pr = pcall(function() return s.maia:policy(before, { nodes = a.maia_nodes }) end)
                if okp and next(pr) ~= nil then rec.priors = pr
                elseif not okp then ctx.log.warn("maia policy failed at ply %d: %s", node.ply, tostring(pr)) end
                p.maia_band = s.maia_band
              end
            end
            p.moves[#p.moves + 1] = rec
            if elo then p.elo_sum = p.elo_sum + elo; p.elo_n = p.elo_n + 1 end
            s.nmoves = s.nmoves + 1
          else
            s.field[#s.field + 1] = rec
          end
          if a.annotate and is_target then
            node.move:setComment(string.format("cpl %d", rec.lcp[rec.chosen]))
          end
        elseif not ok then
          ctx.log.warn("analysis failed at ply %d: %s", node.ply, tostring(rec))
        end
      end
    end
  end
  return input
end

-- Where a suspicious move was played: the opponents tag plus the game URL. For
-- a lichess link, anchor it to the move itself (#ply, half-moves from the
-- start) so the link opens on that position; other hosts get the bare URL.
local function suspect_where(su)
  local tag = su.gtag and ("[" .. su.gtag .. "]") or ""
  local url = su.gurl
  if not url or url == "" then return tag end
  if url:find("lichess%.org") then
    local ply = (su.movno - 1) * 2 + ((su.side == "White") and 1 or 2)
    url = url .. "#" .. ply
  end
  return tag ~= "" and (tag .. " " .. url) or url
end

-- Emit the whole report from an already-merged set of players/field. Kept
-- separate from the accumulation so it runs once, over every worker's games,
-- in reduce(). `a` comes from ctx.scope.args, which init() set on this worker.
local function emit_report(ctx, players, field)
  local a = ctx.scope.args

  local w = ctx.out
  local report = ctx.args:get("report")
  if report then w = ctx.open(report) end
  local emit = function(fmt, ...)
    if w then w:write(string.format(fmt, ...)) else ctx.log.info(fmt, ...) end
  end

  local field_tau = a.reference and field_calibration(field, a.ref_min) or nil
  local function honest_tau_for(elo)
    if field_tau then return field_tau(elo) end
    return builtin_tau(elo, a.cal_a, a.cal_b)
  end

  -- Rank players by posterior so the report leads with the strongest signal.
  local names = {}
  for name, pl in pairs(players) do
    if #pl.moves > 0 then names[#names + 1] = name end
  end

  local results = {}
  for _, name in ipairs(names) do
    local pl = players[name]
    local elo = pl.elo_n > 0 and (pl.elo_sum / pl.elo_n) or a.assume_elo
    local out = evaluate(pl, honest_tau_for(elo), a)
    aggregate(out, a)
    out.name = name
    out.games = pl.games
    out.verdict = verdict(out, a.min_moves)
    out.calib = field_tau and "field" or "built-in"
    results[#results + 1] = out
  end
  table.sort(results, function(x, y) return x.posterior > y.posterior end)

  emit("# tictac cheat report\n")
  emit("# engine-correlation screening -- NOT proof of cheating.\n")
  emit("# calibration: %s%s\n", results[1] and results[1].calib or "built-in",
       field_tau and "" or " (no field reference; honest model is an approximate curve)")
  emit("#\n")

  local function fz(z) return z == nil and "  n/a " or string.format("%+5.2f", z) end
  for _, o in ipairs(results) do
    emit("================================================================\n")
    emit("Player: %s   (%d games, %d analysed moves)\n", o.name, o.games, o.n)
    emit("Verdict: %-18s   P(assisted) = %.1f%%   (combined z=%+.2f, p=%.4f)\n",
         o.verdict, o.posterior * 100, o.Z, o.pvalue)
    emit("----------------------------------------------------------------\n")
    emit("  top-1 match      %.1f%%  (honest ~%.1f%%)         z=%s\n",
         o.t1 * 100, o.exp_match * 100, fz(o.z_match))
    emit("  avg cp loss      %.1f    (honest ~%.1f)           z=%s\n",
         o.acpl, o.exp_acpl, fz(o.z_acpl))
    emit("  clean moves      %.1f%%                            z=%s\n",
         o.clean * 100, fz(o.z_clean))
    if o.z_maia then
      emit("  vs Maia-%d      excellence z=%s  (player plays human move %.0f%% of the time)\n",
           o.maia_band, fz(o.z_maia), o.maia_mean_ph * 100)
      if o.maia_hard and o.maia_hard.p < 0.12 then
        emit("    hardest found: move %d, a 1-in-%d move for this rating\n",
             o.maia_hard.mv, math.max(1, math.floor(1 / math.max(o.maia_hard.p, 1e-4) + 0.5)))
      end
    else
      emit("  hard-move match                                  z=%s\n", fz(o.z_cplx))
    end
    if o.stated_elo then
      emit("  intrinsic rating %4.0f    (stated %4.0f, tau=%.4f)    z=%s\n",
           o.ipr, o.stated_elo, o.tau, fz(o.z_ipr))
    else
      emit("  intrinsic rating %4.0f    (no *Elo header; tau=%.4f)  z= n/a\n",
           o.ipr, o.tau)
    end
    if o.z_time then
      emit("  time vs difficulty r=%+.2f                        z=%s\n",
           o.corr_time, fz(o.z_time))
    elseif (o.n_timed or 0) > 0 then
      emit("  time vs difficulty (%d/15 clocked moves)          z= n/a\n", o.n_timed)
    else
      emit("  time vs difficulty (no clock data)               z= n/a\n")
    end
    if o.seg then
      local sg = o.seg
      emit("  best stretch     moves %d-%d (%d): %.0f%% vs ~%.0f%%  z=%s\n",
           sg.m0, sg.m1, sg.len, sg.rate * 100, sg.exprate * 100, fz(sg.zc))
      if sg.pcorr <= a.seg_alpha then
        emit("    -> FLAGGED stretch: corrected p=%.1e over %d windows scanned\n",
             sg.pcorr, sg.nwin)
      end
    end
    if o.topgames and #o.topgames > 0 then
      emit("  most suspicious games (each scored on its own, strongest first):\n")
      for rank, g in ipairs(o.topgames) do
        emit("    %2d. match %3.0f%% vs ~%2.0f%% over %-3d moves; hottest run %d-%d (%d) %3.0f%% vs ~%2.0f%%%s\n",
             rank, g.gmatch * 100, g.gexp * 100, g.n, g.m0, g.m1, g.len,
             g.rate * 100, g.exprate * 100, g.flagged and "   *** FLAGGED ***" or "")
        emit("        %s\n", suspect_where(g))
      end
    end
    if o.ds_n then
      emit("  deep-only agreement %.0f%% of %d deep-only bests found", o.ds_rateB * 100, o.ds_n)
      if o.ds_rateA then emit("   (vs %.0f%% on obvious bests)", o.ds_rateA * 100) end
      emit("\n")
    end
    if o.suspects and #o.suspects > 0 then
      emit("  most suspicious moves (hardest best-moves found, strongest first):\n")
      for rank, su in ipairs(o.suspects) do
        local mv = su.san or su.uci or "?"
        local g = suspect_where(su)
        if su.maia and su.pplayed then
          emit("    %2d. %-6s move %-3d as %-5s  cpl %-3d  Maia %4.1f%%, played anyway   %s\n",
               rank, mv, su.movno, su.side or "?", su.lcp, su.pplayed * 100, g)
        else
          local odds = su.pm > 0 and math.max(1, math.floor(1 / su.pm + 0.5)) or 0
          emit("    %2d. %-6s move %-3d as %-5s  cpl %-3d  honest match ~%4.1f%% (1-in-%d)   %s\n",
               rank, mv, su.movno, su.side or "?", su.lcp, su.pm * 100, odds, g)
        end
      end
    end
    emit("\n")
  end

  emit("# Each z is oriented so positive = more engine-like, standardized against a\n")
  emit("# null that includes between-player spread (overdispersion), so it reflects\n")
  emit("# how extreme the player is among honest peers, not how many moves were\n")
  emit("# seen. The combined z is a\n")
  emit("# correlation-deflated Stouffer fusion; the posterior applies a prior of\n")
  emit("# %.1f%% and shrinks toward it on small samples. 'best stretch' is the\n",
       a.prior * 100)
  emit("# most engine-like contiguous run within a game, standardized and then\n")
  emit("# corrected for the many overlapping windows scanned; a FLAGGED stretch\n")
  emit("# deserves review even when the whole-game posterior stays low, since a\n")
  emit("# part-game burst is diluted by the honest moves around it. When a Maia\n")
  emit("# network is supplied, 'vs Maia-NNNN' tests whether the player finds\n")
  emit("# excellent moves more often than a human of that rating band does, with\n")
  emit("# Maia's own policy as the honest baseline -- obvious (including forcing)\n")
  emit("# moves count for little, so only genuinely hard moves that were found\n")
  emit("# raise it. The 'most suspicious moves' list ranks individual moves by the\n")
  emit("# same hard-move surprise (Maia excellence excess when a prior is present):\n")
  emit("# a hard position the player nonetheless solved. It is a pointer for review,\n")
  emit("# not a per-move accusation -- an honest strong move looks the same.\n")
  emit("# 'most suspicious games' scores each game on its own -- its hottest run,\n")
  emit("# corrected for the windows in that game and then for the number of games\n")
  emit("# the player has -- so a single engine-like game surfaces here even when the\n")
  emit("# whole-player verdict above stays NO SIGNAL. A FLAGGED game cleared the\n")
  emit("# multiplicity-corrected threshold and deserves direct review.\n")
  emit("# Confirm any flag with a second engine, a deeper pass, and human review\n")
  emit("# before acting.\n")

  if ctx.args:get("csv") then
    local cw = ctx.open(ctx.args:get("csv"))
    cw:write("player,games,moves,t1,exp_t1,acpl,exp_acpl,clean,ipr,stated_elo,tau,"
          .. "z_match,z_acpl,z_clean,z_cplx,z_ipr,z_time,"
          .. "z_seg,seg_from,seg_to,seg_match,seg_pcorr,"
          .. "maia_band,z_maia,maia_mean_ph,Z,pvalue,posterior,verdict\n")
    local function c(v) return v == nil and "" or string.format("%.4f", v) end
    for _, o in ipairs(results) do
      local sg = o.seg
      cw:writef("%s,%d,%d,%s,%s,%s,%s,%s,%.0f,%s,%.4f,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%.3f,%.5f,%.4f,%s\n",
        o.name, o.games, o.n, c(o.t1), c(o.exp_match), c(o.acpl), c(o.exp_acpl),
        c(o.clean), o.ipr, o.stated_elo and string.format("%.0f", o.stated_elo) or "",
        o.tau, c(o.z_match), c(o.z_acpl), c(o.z_clean), c(o.z_cplx), c(o.z_ipr),
        c(o.z_time), c(sg and sg.zc), sg and string.format("%d", sg.m0) or "",
        sg and string.format("%d", sg.m1) or "", c(sg and sg.rate),
        sg and string.format("%.3e", sg.pcorr) or "",
        o.maia_band and string.format("%d", o.maia_band) or "", c(o.z_maia), c(o.maia_mean_ph),
        o.Z, o.pvalue, o.posterior, o.verdict)
    end
  end
end

-- Cross-worker aggregation. The engine-bound work (process) runs in parallel at
-- -j N; each worker accumulates its own games. partial() hands this worker's
-- raw tally over, and reduce() folds every worker's tally into one before the
-- single emit, so the report is identical to a -j1 run.
function plugin.partial(ctx)
  local s = ctx.scope
  return { players = s.players, field = s.field, nmoves = s.nmoves }
end

function plugin.reduce(partials, ctx)
  local players, field = {}, {}
  for _, part in ipairs(partials) do
    if part and part.players then
      for name, p in pairs(part.players) do
        local m = players[name]
        if not m then
          m = { moves = {}, games = 0, wins = 0, elo_sum = 0, elo_n = 0, maia_band = p.maia_band }
          players[name] = m
        end
        m.maia_band = m.maia_band or p.maia_band
        m.games   = m.games + (p.games or 0)
        m.wins    = m.wins + (p.wins or 0)
        m.elo_sum = m.elo_sum + (p.elo_sum or 0)
        m.elo_n   = m.elo_n + (p.elo_n or 0)
        -- Game ids are per-worker (1..g); remap to a global, contiguous range so
        -- the segment scan never merges windows across two workers' games. The
        -- remap is per worker, so the same local id in another worker gets a new
        -- global one; the counter carries over between workers via m.nextgid.
        local remap, nextg = {}, m.nextgid or 0
        for _, rec in ipairs(p.moves or {}) do
          local old = rec.gi or 1
          if not remap[old] then nextg = nextg + 1; remap[old] = nextg end
          rec.gi = remap[old]
          m.moves[#m.moves + 1] = rec
        end
        m.nextgid = nextg
      end
      for _, rec in ipairs(part.field or {}) do field[#field + 1] = rec end
    end
  end
  for _, m in pairs(players) do m.nextgid = nil end
  emit_report(ctx, players, field)
end

return plugin
end)()

-- vim:ft=lua:ts=2:sw=2:
