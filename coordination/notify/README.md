# Notification Protocol (Optional)

Sends one-line alerts to Discord via webhook.
Silent no-op if config is missing.
Git remains the source of truth.

## Setup

1. Create a Discord server / channel named #agents.
2. Server Settings → Integrations → Webhooks → New.
3. Copy the webhook URL.
4. Create coordination/notify/config locally:

     WEBHOOK_URL=https://discord.com/api/webhooks/...

5. Never commit config. Only config.example is tracked.

## Usage

  python coordination/notify/notify.py "<agent>: <msg>"
