#include "creditsdialog.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFrame>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>

namespace {

struct Credit {
    const char *name;
    const char *reason;
};

const Credit Thanks[] = {
    {"Becko", QT_TRANSLATE_NOOP("CreditsDialog", "Huge thanks. The main inspiration for this project, and the packet captures that made it possible.")},
    {"LazyLemon", QT_TRANSLATE_NOOP("CreditsDialog", "Info on how things looked in old versions, help with the launcher, founding the r/OpenKogama subreddit, and translating the website into Russian.")},
    {"bustersky", QT_TRANSLATE_NOOP("CreditsDialog", "Archiving the standalone builds.")},
    {"kamilslimak", QT_TRANSLATE_NOOP("CreditsDialog", "Finding the 2012 build.")},
    {"Dylinn", QT_TRANSLATE_NOOP("CreditsDialog", "Announcing OpenKogama on Regama.")},
    {"Zpayer", QT_TRANSLATE_NOOP("CreditsDialog", "Announcing OpenKogama.")},
    {"loafio", QT_TRANSLATE_NOOP("CreditsDialog", "Sharing OpenKogama on his server.")},
    {"Limeen", QT_TRANSLATE_NOOP("CreditsDialog", "Translating the website into French.")},
    {"lordscorpioni", QT_TRANSLATE_NOOP("CreditsDialog", "Translating the website and the launcher into Spanish.")},
};

QFont brandFont(int pointSize)
{
    static const QString family = [] {
        int id = QFontDatabase::addApplicationFont(":/openkogama-font.ttf");
        QStringList families = QFontDatabase::applicationFontFamilies(id);
        return families.isEmpty() ? QString() : families.first();
    }();
    QFont font(family);
    font.setPointSize(pointSize);
    return font;
}

QLabel *secondary(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}

}

CreditsDialog::CreditsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Credits"));
    setWindowIcon(QIcon(":/logo.png"));
    setFixedWidth(380);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 16);
    layout->setSpacing(6);

    auto *logo = new QLabel(this);
    logo->setPixmap(QPixmap(":/logo.png").scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setAlignment(Qt::AlignCenter);
    layout->addWidget(logo);

    auto *title = new QLabel("OPENKOGAMA", this);
    title->setFont(brandFont(26));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto *author = secondary(tr("Made by %1").arg("nightus"), this);
    author->setAlignment(Qt::AlignCenter);
    layout->addWidget(author);

    layout->addSpacing(12);
    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);
    layout->addSpacing(8);

    auto *heading = new QLabel(tr("Thanks to"), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 1);
    heading->setFont(headingFont);
    layout->addWidget(heading);

    for (const Credit &credit : Thanks) {
        layout->addSpacing(6);
        auto *name = new QLabel(credit.name, this);
        QFont nameFont = name->font();
        nameFont.setBold(true);
        name->setFont(nameFont);
        layout->addWidget(name);
        layout->addWidget(secondary(tr(credit.reason), this));
    }

    layout->addSpacing(16);
    layout->addStretch();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}
