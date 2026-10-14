# ASTRA web — static SPA image.
#
# Stage 1 builds the bundle (Vite env vars must be present at BUILD time,
# because Vite inlines them). Stage 2 serves the static output with nginx.
#
# Build:  docker build -t astra-frontend --build-arg VITE_API_BASE_URL=https://gateway.example/api/v1/ .
# Run:    docker run --rm -p 8080:8080 astra-frontend

FROM node:20-alpine AS build
WORKDIR /app

# Reproducible installs from the committed lockfile.
COPY package.json package-lock.json ./
RUN npm ci --no-audit --no-fund

COPY tsconfig.json vite.config.ts index.html ./
COPY public ./public
COPY src ./src
COPY tests ./tests
COPY scripts ./scripts
COPY docs ./docs

# Same-origin API path by default; override for a dedicated gateway origin.
ARG VITE_API_BASE_URL=""
ARG VITE_DEMO_MODE=""
ENV VITE_API_BASE_URL=$VITE_API_BASE_URL \
    VITE_DEMO_MODE=$VITE_DEMO_MODE
RUN npm run build

FROM nginx:1.27-alpine AS runtime

# Consumed as an nginx template so that ${PORT} (Railway/Docker) is substituted
# at container start; PORT defaults to 8080 for docker run / docker compose.
ENV PORT=8080
COPY nginx.conf /etc/nginx/templates/default.conf.template
COPY --from=build /app/dist /usr/share/nginx/html

EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=3s --retries=3 \
  CMD wget -qO- "http://127.0.0.1:${PORT}/" >/dev/null 2>&1 || exit 1
