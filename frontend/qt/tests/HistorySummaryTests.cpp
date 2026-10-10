// Offscreen widget tests for HistoryPage summary formatting.
//
// Regression teeth for the realized-summary format strings. The "Total R" cell
// used QString("%+1.%1R") and the "PF" cell used QString("1.%1"); both are
// malformed (a literal '1' plus the numbered placeholder reinserts the value),
// so they rendered garbage like "%+1.3.5R" and "1.1.23" instead of "+3.5R"
// and "1.23". updateFromHistory only yields "pending" rows today, so the bug
// is latent but reachable; these tests drive updateSummary directly.

#include "pages/HistoryPage.h"

#include <QApplication>
#include <QFrame>
#include <QLabel>

#include <cstdio>

using namespace astra;

namespace {

int g_failures = 0;

void check(bool ok, const char* name, const QString& detail = QString()) {
    if (!ok) {
        std::fprintf(stderr, "  CHECK failed: %s %s\n", name,
                     qPrintable(detail));
        ++g_failures;
    } else {
        std::printf("[PASS] %s\n", name);
    }
}

QString summaryValue(const HistoryPage& page, const char* caption) {
    const auto labels = page.findChildren<QLabel*>();
    for (QLabel* l : labels) {
        if (l->text() == caption) {
            // The caption and its value share a card; the value is the next
            // QLabel sibling in the card layout.
            if (QWidget* card = l->parentWidget()) {
                const auto cardLabels = card->findChildren<QLabel*>();
                if (cardLabels.size() >= 2 && cardLabels.first() == l) {
                    return cardLabels.at(1)->text();
                }
            }
        }
    }
    return QString("<not found>");
}

HistoryEntry entry(const char* outcome, double r) {
    HistoryEntry e;
    e.outcome = outcome;
    e.rMultiple = r;
    return e;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    HistoryPage page;

    // Empty state: nothing is fabricated, every summary stays unavailable.
    check(summaryValue(page, "Win Rate") == QString::fromUtf8("\u2014"),
          "empty win rate is em dash", summaryValue(page, "Win Rate"));
    check(summaryValue(page, "Total R") == QString::fromUtf8("\u2014"),
          "empty total R is em dash", summaryValue(page, "Total R"));
    check(summaryValue(page, "PF") == QString::fromUtf8("\u2014"),
          "empty PF is em dash", summaryValue(page, "PF"));

    // One win (+3.0R) and one loss (-1.0R):
    //   win rate = 50%, total R = +2.0R, PF = |2.0/2| = 1.00
    QVector<HistoryEntry> entries;
    entries.append(entry("win", 3.0));
    entries.append(entry("loss", -1.0));
    page.setSummaryEntriesForTest(entries);

    check(summaryValue(page, "Win Rate") == "50%",
          "win rate is 50%", summaryValue(page, "Win Rate"));
    check(summaryValue(page, "Total R") == "+2.0R",
          "total R is +2.0R (no literal 1)", summaryValue(page, "Total R"));
    check(summaryValue(page, "PF") == "1.00",
          "PF is 1.00 (no literal 1 prefix)", summaryValue(page, "PF"));

    // A single loss only: total R negative, no bogus placeholder text.
    QVector<HistoryEntry> losing;
    losing.append(entry("loss", -2.5));
    page.setSummaryEntriesForTest(losing);
    check(summaryValue(page, "Total R") == "-2.5R",
          "negative total R is -2.5R", summaryValue(page, "Total R"));
    check(summaryValue(page, "PF") == "2.50",
          "negative PF magnitude is 2.50", summaryValue(page, "PF"));

    if (g_failures == 0) {
        std::printf("HistoryPage summary tests: all passed\n");
    } else {
        std::fprintf(stderr, "HistoryPage summary tests: %d failed\n",
                     g_failures);
    }
    return g_failures == 0 ? 0 : 1;
}
