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
    {"Becko", "Huge thanks. The main inspiration for this project, and the packet captures that made it possible."},
    {"LazyLemon", "Info on how things looked in old versions, and help with the launcher."},
    {"bustersky", "Archiving the standalone builds."},
    {"kamilslimak", "Finding the 2012 build."},
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
    setWindowTitle("Credits");
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

    auto *author = secondary("Made by nightus", this);
    author->setAlignment(Qt::AlignCenter);
    layout->addWidget(author);

    layout->addSpacing(12);
    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);
    layout->addSpacing(8);

    auto *heading = new QLabel("Thanks to", this);
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
        layout->addWidget(secondary(credit.reason, this));
    }

    layout->addSpacing(16);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}
