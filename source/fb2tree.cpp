#include "fb2tree.hpp"

#include <QtDebug>
#include <QWebFrame>
#include <QWebPage>
#include <QTreeView>
#include <QUrl>

#include "fb2utils.h"
#include "fb2view.hpp"

Fb2TreeItem::Fb2TreeItem(QWebElement &element, Fb2TreeItem *parent)
    : QObject(parent)
    , m_element(element)
    , m_parent(parent)
{
    m_name = element.tagName().toLower();
    QString style = element.attribute("class").toLower();
    if (m_name == "div") {
        if (style == "title") {
            m_text = title(element);
            if (m_parent) m_parent->m_text += m_text += " ";
        } else if (style == "subtitle") {
            m_text = title(element);
        } else if (style == "body") {
            QString name = element.attribute("name");
            if (!name.isEmpty()) style += " name=" + name;
        }
        if (!style.isEmpty()) m_name = style;
    } else if (m_name == "img") {
        QUrl url = element.attribute("src");
        m_text = url.path();
    }
    addChildren(element);
}

Fb2TreeItem::~Fb2TreeItem()
{
    foreach (Fb2TreeItem * item, m_list) {
        delete item;
    }
}

QString Fb2TreeItem::title(const QWebElement &element)
{
    return element.toPlainText().left(255).simplified();
}

void Fb2TreeItem::addChildren(QWebElement &parent, bool direct)
{
    QWebElement child = parent.firstChild();
    while (!child.isNull()) {
        QString tag = child.tagName().toLower();
        if (tag == "div") {
            Fb2TreeItem * item = new Fb2TreeItem(child, this);
            m_list << item;
            if (direct) m_content << item;
        } else if (tag == "img") {
            Fb2TreeItem * item = new Fb2TreeItem(child, this);
            m_list << item;
            if (direct) m_content << item;
        } else {
            addChildren(child, false);
        }
        child = child.nextSibling();
    }
}

Fb2TreeItem * Fb2TreeItem::item(const QModelIndex &index) const
{
    int row = index.row();
    if (row < 0 || row >= m_list.size()) return NULL;
    return m_list[row];
}

Fb2TreeItem * Fb2TreeItem::item(int row) const
{
    if (row < 0 || row >= m_list.size()) return NULL;
    return m_list[row];
}

QString Fb2TreeItem::text() const
{
    return QString("<%1> %2").arg(m_name).arg(m_text);
}

QString Fb2TreeItem::selector() const
{
    QString text = "";
    QString selector = ".get(0)";
    QWebElement element = m_element;
    QWebElement parent = element.parent();
    while (!parent.isNull()) {
        text.prepend(element.tagName()).prepend("/");
        QWebElement child = parent.firstChild();
        int index = -1;
        while (!child.isNull()) {
            index++;
            if (child == element) break;
            child = child.nextSibling();
        }
        if (index == -1) return QString();
        selector.prepend(QString(".children().eq(%1)").arg(index));
        element = parent;
        parent = element.parent();
    }
    return selector.prepend("$('html')");
}

Fb2TreeItem * Fb2TreeItem::content(int index) const
{
    return (0 <= index && index < m_content.count()) ? m_content[index] : 0;
}

//---------------------------------------------------------------------------
//  Fb2TreeModel
//---------------------------------------------------------------------------

Fb2TreeModel::Fb2TreeModel(Fb2WebView &view, QObject *parent)
    : QAbstractItemModel(parent)
    , m_view(view)
    , m_root(NULL)
{
    QWebElement doc = view.page()->mainFrame()->documentElement();
    QWebElement body = doc.findFirst("body");
    if (body.isNull()) return;
    m_root = new Fb2TreeItem(body);
}

Fb2TreeModel::~Fb2TreeModel()
{
    if (m_root) delete m_root;
}

void Fb2TreeModel::expand(QTreeView *view)
{
    QModelIndex parent = QModelIndex();
    int count = rowCount(parent);
    for (int i = 0; i < count; i++) {
        QModelIndex child = index(i, 0, parent);
        Fb2TreeItem *node = item(child);
        if (node && node->name() == "body") view->expand(child);
    }
}

Fb2TreeItem * Fb2TreeModel::item(const QModelIndex &index) const
{
    if (index.isValid()) {
        return static_cast<Fb2TreeItem*>(index.internalPointer());
    } else {
        return m_root;
    }
}

int Fb2TreeModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 1;
}

QModelIndex Fb2TreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!m_root || row < 0 || column < 0) return QModelIndex();
    if (Fb2TreeItem *owner = item(parent)) {
        if (Fb2TreeItem *child = owner->item(row)) {
            return createIndex(row, column, (void*)child);
        }
    }
    return QModelIndex();
}

QModelIndex Fb2TreeModel::parent(const QModelIndex &child) const
{
    if (Fb2TreeItem * node = static_cast<Fb2TreeItem*>(child.internalPointer())) {
        if (Fb2TreeItem * parent = node->parent()) {
            if (Fb2TreeItem * owner = parent->parent()) {
                return createIndex(owner->index(parent), 0, (void*)parent);
            }
        }
    }
    return QModelIndex();
}

int Fb2TreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0) return 0;
    Fb2TreeItem *owner = item(parent);
    return owner ? owner->count() : 0;
}

QVariant Fb2TreeModel::data(const QModelIndex &index, int role) const
{
    if (role != Qt::DisplayRole) return QVariant();
    Fb2TreeItem * i = item(index);
    return i ? i->text() : QVariant();
}

void Fb2TreeModel::select(const QModelIndex &index)
{
    Fb2TreeItem *node = item(index);
    QWebFrame *frame = m_view.page()->mainFrame();
    if (node) frame->scroll(0, node->pos().y() - frame->scrollPosition().y());

    static const QString setCursor = FB2::read(":/js/set_cursor.js");
    QString javascript = QString("var element=%1;").arg(node->selector()) + setCursor;
    frame->evaluateJavaScript(javascript);

    m_view.setFocus();
}

QModelIndex Fb2TreeModel::index(const QString &location) const
{
    QModelIndex index;
    Fb2TreeItem * parent = m_root;
    QStringList list = location.split(",");
    QStringListIterator iterator(list);
    while (parent && iterator.hasNext()) {
        QString str = iterator.next();
        if (str.left(5) == "HTML=") continue;
        int key = str.mid(str.indexOf("=")+1).toInt();
        Fb2TreeItem * child = parent->content(key);
        if (child) index = this->index(key, 0, index);
        parent = child;
    }
    return QModelIndex();
}

//---------------------------------------------------------------------------
//  Fb2TreeView
//---------------------------------------------------------------------------

void Fb2TreeView::select()
{
    return;
    if (model() == 0) return;
    Fb2TreeModel * model = static_cast<Fb2TreeModel*>(this->model());
    QWebFrame * frame = model->view().page()->mainFrame();
    static const QString javascript = FB2::read(":/js/get_location.js");
    QString location = frame->evaluateJavaScript(javascript).toString();
    QModelIndex index = model->index(location);
    setCurrentIndex(index);
    this->expand(index);
}

void Fb2TreeView::update()
{
}
