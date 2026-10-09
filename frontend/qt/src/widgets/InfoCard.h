#pragma once
#include <QFrame>
#include <QString>
#include <QVector>
#include <QEvent>

class QLabel;
class QVBoxLayout;
class QHBoxLayout;
class QTableWidget;

namespace astra {

// ──────────────────────────────────────────────────────────────────────────────
// InfoCard — one titled card on a governance page.
//   title (card caption) + body rows of "caption ......... value".
// Values default to the em dash and are only replaced with real data; nothing
// is fabricated. Theme-following (re-reads the palette on PaletteChange).
//
// Also provides shared helpers so every governance page renders unknown data
// identically:
//   kEmDash       — "\u2014"
//   orDash(s)     — s if non-empty, else the em dash
//   makeEmptyState(parent, text) — the honest "no data" line
// ──────────────────────────────────────────────────────────────────────────────

constexpr const char* kEmDash = "\u2014";

QString orDash(const QString& value);
QString orUnavailable(bool have);

// A left-aligned secondary label used for "nothing here yet" states.
QLabel* makeEmptyState(QWidget* parent, const QString& text);

// A read-only, non-selectable table in the shared ledger style. Centralised so
// every governance page renders tabular data identically.
QTableWidget* makeLedgerTable(QWidget* parent, const QStringList& headers,
                              int minHeight = 120);

// Replace the table body with a single spanned "nothing here" row.
void fillLedgerEmpty(QTableWidget* table, int cols, const QString& text);

// (Re)apply the ledger stylesheet for the table's current palette. Call from a
// page's restyle() so tables follow theme switches.
void styleLedgerTable(QTableWidget* table);

class InfoCard : public QFrame {
    Q_OBJECT

public:
    explicit InfoCard(QWidget* parent = nullptr);

    void setTitle(const QString& title);

    // Append a caption/value row. Returns the value label so the caller can
    // set semantic colors later. The initial value is the em dash unless a
    // value is supplied.
    QLabel* addRow(const QString& caption, const QString& value = QString());

    // Append a caption-only line (section sub-heading).
    QLabel* addCaption(const QString& caption);

    // Append an arbitrary widget (e.g. a table or empty state) to the body.
    void addWidget(QWidget* widget);

    QVBoxLayout* body() const { return mBody; }

protected:
    void changeEvent(QEvent* event) override;

private:
    void restyle();

    QLabel* mTitle = nullptr;
    QVBoxLayout* mBody = nullptr;
    QVector<QLabel*> mRowCaptions;
    QVector<QLabel*> mRowValues;
    QVector<QFrame*> mDividers;
};

}  // namespace astra
