#include <QTest>
class X : public QObject { Q_OBJECT private slots: void a() {} };
QTEST_MAIN(X)
#include "test_canvas_input.moc"
