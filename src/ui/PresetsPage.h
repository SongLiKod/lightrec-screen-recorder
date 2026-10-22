#pragma once

#include "core/Types.h"

#include <QWidget>

class QLabel;
class QListWidget;

namespace lightrec {

class PresetManager;

class PresetsPage : public QWidget
{
    Q_OBJECT
public:
    explicit PresetsPage(PresetManager *presets, QWidget *parent = nullptr);

    void refresh();
    void selectPreset(const QString &name);

signals:
    void applyRequested(const lightrec::Preset &preset);
    void saveCurrentRequested(const QString &name);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void updateDetails();
    QString selectedName() const;

    PresetManager *m_presets = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_details = nullptr;
};

} // namespace lightrec
