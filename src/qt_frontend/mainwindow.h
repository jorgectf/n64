#ifndef N64_MAIN_WINDOW
#define N64_MAIN_WINDOW

#include <QMainWindow>
#include <QVulkanWindow>

#include "vulkan_pane.h"

QT_BEGIN_NAMESPACE
namespace Ui {
    class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    virtual ~MainWindow() {};

    void showEvent(QShowEvent* event) override;

public slots:
    void resetTriggered();
    void openFileTriggered();

private:
    Ui::MainWindow *ui;
    VulkanPane* vkPane;
};


#endif // N64_MAIN_WINDOW