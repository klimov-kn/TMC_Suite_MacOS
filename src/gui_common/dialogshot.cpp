#include "dialogshot.h"

#include <QApplication>
#include <QDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QDir>
#include <QPixmap>

namespace tmcshot {

bool grabDialog(UINT idd, const QString &fileName)
{
    if (!MfcDialogRegistry::setupFor(idd))
        return false;

    // Диалог строится тем же кодом, что и в работе программы: разметка из
    // ресурсов Windows, подгонка под содержимое, оформление.
    CDialog dialog(idd);
    QDialog window;
    dialog.TmcBuildIn(&window);

    // Заполняем поля пробным текстом: пустое поле не показывает, помещается ли
    // в него строка. Это проверка внешнего вида, а не поведения программы.
    for (QLineEdit* edit : window.findChildren<QLineEdit*>()) {
        if (edit->text().isEmpty())
            edit->setText(QStringLiteral("Пример 123.456"));
    }
    for (QPlainTextEdit* edit : window.findChildren<QPlainTextEdit*>()) {
        if (edit->toPlainText().isEmpty())
            edit->setPlainText(QStringLiteral("Пример имени файла.soc"));
    }

    window.show();
    // Даём окну разложиться: размеры вычисляются при первом показе.
    QApplication::processEvents();
    window.adjustSize();
    QApplication::processEvents();

    const QPixmap shot = window.grab();
    const bool ok = shot.save(fileName);

    window.hide();
    dialog.TmcDetach();
    return ok;
}

int grabAllDialogs(const QString &directory)
{
    QDir().mkpath(directory);
    int count = 0;
    for (UINT idd : MfcDialogRegistry::registeredDialogs()) {
        const QString file = QStringLiteral("%1/dialog_%2.png")
                                 .arg(directory)
                                 .arg(uint(idd));
        if (grabDialog(idd, file))
            ++count;
    }
    return count;
}

} // namespace tmcshot
