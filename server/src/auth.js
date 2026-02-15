const bcrypt = require('bcryptjs');

async function hashPassword(password) {
  const pw = String(password || '');
  // bcryptjs is pure JS; cost 10 is a reasonable default for a small VPS.
  const saltRounds = 10;
  return bcrypt.hash(pw, saltRounds);
}

async function verifyPassword(password, passwordHash) {
  try {
    return await bcrypt.compare(String(password || ''), String(passwordHash || ''));
  } catch (e) {
    return false;
  }
}

function sessionUser(req) {
  return req && req.session && req.session.user ? req.session.user : null;
}

function requireAuthApi(req, res, next) {
  if (sessionUser(req)) return next();
  res.status(401).json({ ok: false, error: 'unauthorized' });
}

function requireAuthPage(req, res, next) {
  if (sessionUser(req)) return next();
  const nextUrl = encodeURIComponent(req.originalUrl || '/');
  res.redirect('/login?next=' + nextUrl);
}

function requireAdmin(req, res, next) {
  const u = sessionUser(req);
  if (u && u.role === 'admin') return next();
  res.status(403).json({ ok: false, error: 'forbidden' });
}

module.exports = {
  hashPassword,
  verifyPassword,
  sessionUser,
  requireAuthApi,
  requireAuthPage,
  requireAdmin
};

