// SPDX-License-Identifier: GPL-2.0
#include "qt-models/divelistmodel.h"
#include "core/qthelper.h"
#include "core/trip.h"
#include "core/settings/qPrefGeneral.h"
#include <QDateTime>

DiveListSortModel::DiveListSortModel(QObject *parent) : QSortFilterProxyModel(parent)
{
	updateFilterState();
}

void DiveListSortModel::updateFilterState()
{
	if (filterString.isEmpty()) {
		resetFilter();
		return;
	}
	// store this in local variables to avoid having to call these methods over and over
	bool includeNotes = qPrefGeneral::filterFullTextNotes();
	Qt::CaseSensitivity cs = qPrefGeneral::filterCaseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive;

	int i;
	struct dive *d;
	for_each_dive(i, d)
		d->hidden_by_filter = !DiveObjectHelper::containsText(d, filterString, cs, includeNotes);
}

void DiveListSortModel::setSourceModel(QAbstractItemModel *sourceModel)
{
	QSortFilterProxyModel::setSourceModel(sourceModel);
}

void DiveListSortModel::setFilter(QString f)
{
	filterString = f;
	updateFilterState();
	invalidateFilter();
}

void DiveListSortModel::resetFilter()
{
	int i;
	struct dive *d;
	for_each_dive(i, d)
		d->hidden_by_filter = false;
	invalidateFilter();
}

// filtering is way too slow on mobile. Maybe we should roll our own?
bool DiveListSortModel::filterAcceptsRow(int source_row, const QModelIndex &) const
{
	DiveListModel *mySourceModel = qobject_cast<DiveListModel *>(sourceModel());
	DiveObjectHelper d = mySourceModel->at(source_row);
	return d && !d.getDive()->hidden_by_filter;
}

int DiveListSortModel::shown()
{
	return rowCount();
}

int DiveListSortModel::getIdxForId(int id)
{
	for (int i = 0; i < rowCount(); i++) {
		QVariant v = data(index(i, 0), DiveListModel::DiveRole);
		DiveObjectHelper d = v.value<DiveObjectHelper>();
		if (d.id() == id)
			return i;
	}
	return -1;
}

void DiveListSortModel::reload()
{
	DiveListModel *mySourceModel = qobject_cast<DiveListModel *>(sourceModel());
	mySourceModel->reload();
}

// In QML, section headings can only be strings. To identify dives that
// belong to the same trip, a string containing the trip-pointer in hexadecimal
// encoding is passed in. To format the trip heading, the string is then
// converted back with this function.
QVariant DiveListSortModel::tripIdToObject(const QString &s)
{
	if (s.isEmpty())
		return QVariant();
	return QVariant::fromValue((dive_trip *)s.toULongLong(nullptr, 16));
}

// the trip title is designed to be location (# dives)
// or, if there is no location name date range (# dives)
// where the date range is given as "month year" or "month-month year" or "month year - month year"
QString DiveListSortModel::tripTitle(const QVariant &tripIn)
{
	dive_trip *dt = tripIn.value<dive_trip *>();
	if (!dt)
		return QString();
	QString numDives = tr("(%n dive(s))", "", dt->dives.nr);
	int shown = trip_shown_dives(dt);
	QString shownDives = shown != dt->dives.nr ? QStringLiteral(" ") + tr("(%L1 shown)").arg(shown) : QString();
	QString title(dt->location);

	if (title.isEmpty()) {
		// so use the date range
		QDateTime firstTime = QDateTime::fromMSecsSinceEpoch(1000*trip_date(dt), Qt::UTC);
		QString firstMonth = firstTime.toString("MMM");
		QString firstYear = firstTime.toString("yyyy");
		QDateTime lastTime = QDateTime::fromMSecsSinceEpoch(1000*dt->dives.dives[0]->when, Qt::UTC);
		QString lastMonth = lastTime.toString("MMM");
		QString lastYear = lastTime.toString("yyyy");
		if (lastMonth == firstMonth && lastYear == firstYear)
			title = firstMonth + " " + firstYear;
		else if (lastMonth != firstMonth && lastYear == firstYear)
			title = firstMonth + "-" + lastMonth + " " + firstYear;
		else
			title = firstMonth + " " + firstYear + " - " + lastMonth + " " + lastYear;
	}
	return QStringLiteral("%1 %2%3").arg(title, numDives, shownDives);
}

QString DiveListSortModel::tripShortDate(const QVariant &tripIn)
{
	dive_trip *dt = tripIn.value<dive_trip *>();
	if (!dt)
		return QString();
	QDateTime firstTime = QDateTime::fromMSecsSinceEpoch(1000*trip_date(dt), Qt::UTC);
	QString firstMonth = firstTime.toString("MMM");
	return QStringLiteral("%1\n'%2").arg(firstMonth,firstTime.toString("yy"));
}

DiveListModel *DiveListModel::m_instance = NULL;

DiveListModel::DiveListModel(QObject *parent) : QAbstractListModel(parent)
{
	m_instance = this;
}

void DiveListModel::insertDive(int i)
{
	beginInsertRows(QModelIndex(), i, i);
	endInsertRows();
}

void DiveListModel::removeDive(int i)
{
	beginRemoveRows(QModelIndex(), i, i);
	endRemoveRows();
}

void DiveListModel::removeDiveById(int id)
{
	for (int i = 0; i < dive_table.nr; i++) {
		if (dive_table.dives[i]->id == id) {
			removeDive(i);
			return;
		}
	}
}

void DiveListModel::updateDive(int i, dive *d)
{
	// we need to make sure that QML knows that this dive has changed -
	// the only reliable way I've found is to remove and re-insert it
	removeDive(i);
	insertDive(i);
}

void DiveListModel::reload()
{
	beginResetModel();
	endResetModel();
}

void DiveListModel::resetInternalData()
{
	// this is a hack. There is a long standing issue, that seems related to a
	// sync problem between QML engine and underlying model data. It causes delete
	// from divelist (on mobile) to crash. But not always. This function is part of
	// an attempt to fix this. See commit.
	beginResetModel();
	endResetModel();
}

int DiveListModel::rowCount(const QModelIndex &) const
{
	return dive_table.nr;
}

int DiveListModel::getDiveIdx(int id) const
{
	return get_idx_by_uniq_id(id);
}

QVariant DiveListModel::data(const QModelIndex &index, int role) const
{
	if(index.row() < 0 || index.row() >= dive_table.nr)
		return QVariant();

	dive *d = dive_table.dives[index.row()];
	if (!d)
		return QVariant();
	switch(role) {
	case DiveRole: return QVariant::fromValue(DiveObjectHelper(d));
	case DiveDateRole: return (qlonglong)d->when;
	case TripIdRole: return d->divetrip ? QString::number((quint64)d->divetrip, 16) : QString();
	case TripNrDivesRole: return d->divetrip ? d->divetrip->dives.nr : 0;
	case DateTimeRole: {
		QDateTime localTime = QDateTime::fromMSecsSinceEpoch(1000 * d->when, Qt::UTC);
		localTime.setTimeSpec(Qt::UTC);
		return QStringLiteral("%1 %2").arg(localTime.date().toString(prefs.date_format_short),
						   localTime.time().toString(prefs.time_format));
		}
	case IdRole: return d->id;
	case NumberRole: return d->number;
	case LocationRole: return get_dive_location(d);
	case DepthDurationRole: return QStringLiteral("%1 / %2").arg(get_depth_string(d->dc.maxdepth.mm, true, true),
								     get_dive_duration_string(d->duration.seconds, gettextFromC::tr("h"), gettextFromC::tr("min")));
	}
	return QVariant();
}

QHash<int, QByteArray> DiveListModel::roleNames() const
{
	QHash<int, QByteArray> roles;
	roles[DiveRole] = "dive";
	roles[DiveDateRole] = "date";
	roles[TripIdRole] = "tripId";
	roles[TripNrDivesRole] = "tripNrDives";
	roles[DateTimeRole] = "dateTime";
	roles[IdRole] = "id";
	roles[NumberRole] = "number";
	roles[LocationRole] = "location";
	roles[DepthDurationRole] = "depthDuration";
	return roles;
}

// create a new dive. set the current time and add it to the end of the dive list
QString DiveListModel::startAddDive()
{
	struct dive *d;
	d = alloc_dive();
	d->when = QDateTime::currentMSecsSinceEpoch() / 1000L + gettimezoneoffset();

	// find the highest dive nr we have and pick the next one
	struct dive *pd;
	int i, nr = 0;
	for_each_dive(i, pd) {
		if (pd->number > nr)
			nr = pd->number;
	}
	nr++;
	d->number = nr;
	d->dc.model = strdup("manually added dive");
	append_dive(d);
	insertDive(get_idx_by_uniq_id(d->id));
	return QString::number(d->id);
}

DiveListModel *DiveListModel::instance()
{
	return m_instance;
}

DiveObjectHelper DiveListModel::at(int i)
{
	if (i < 0 || i >= dive_table.nr) {
		qWarning("DiveListModel::at(): accessing invalid dive with id %d", i);
		return DiveObjectHelper(); // Returns an invalid DiveObjectHelper that will crash on access.
	}
	return DiveObjectHelper(dive_table.dives[i]);
}
