/**
 * Proxy configuration for Create React App
 * Routes API requests to the local Django backend
 */
const { createProxyMiddleware } = require('http-proxy-middleware');

module.exports = function(app) {
  console.log('[setupProxy] Configuring proxy to http://localhost:8000');
  
  // Proxy for /api/ - Express strips the /api prefix, so we re-add it
  app.use('/api', createProxyMiddleware({
    target: 'http://localhost:8000',
    changeOrigin: true,
    cookieDomainRewrite: 'localhost',
    pathRewrite: { '^/': '/api/' },  // Re-add /api/ prefix that Express stripped
    onProxyReq: (proxyReq, req, res) => {
      console.log(`[Proxy] ${req.method} /api${req.url} -> localhost:8000`);
    },
    onError: (err, req, res) => {
      console.error('[Proxy Error]', err.message);
    }
  }));

  // Proxy for other endpoints (keep full path)
  const otherProxy = createProxyMiddleware({
    target: 'http://localhost:8000',
    changeOrigin: true,
    cookieDomainRewrite: 'localhost',
  });

  app.use('/login', otherProxy);
  app.use('/logout', otherProxy);
  app.use('/admin', otherProxy);
  app.use('/saml', otherProxy);
  
  console.log('[setupProxy] Proxy configured');
};
