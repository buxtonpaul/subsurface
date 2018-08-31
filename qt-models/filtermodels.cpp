// SPDX-License-Identifier: GPL-2.0
#include "qt-models/filtermodels.h"
#include "qt-models/models.h"
#include "core/display.h"
#include "core/subsurface-string.h"
#include "qt-models/divetripmodel.h"

#if !defined(SUBSURFACE_MOBILE)
#include "desktop-widgets/divelistview.h"
#include "desktop-widgets/mainwindow.h"
#endif

#include <QDebug>
#include <algorithm>

#define CREATE_INSTANCE_METHOD(CLASS)             \
	CLASS *CLASS::instance()                  \
	{                                         \
		static CLASS *self = new CLASS(); \
		return self;                      \
	}

CREATE_INSTANCE_METHOD(TagFilterModel)
CREATE_INSTANCE_METHOD(BuddyFilterModel)
CREATE_INSTANCE_METHOD(LocationFilterModel)
CREATE_INSTANCE_METHOD(SuitsFilterModel)
CREATE_INSTANCE_METHOD(MultiFilterSortModel)

FilterModelBase::FilterModelBase(QObject *parent) : QStringListModel(parent),
	anyChecked(false),
	negate(false)
{
}

// Update the stringList and the items array.
// The last item is supposed to be the "Show Empty Tags" entry.
void FilterModelBase::updateList(const QStringList &newList)
{
	// Keep copy of the old items array to reimport the checked state later.
	// Note that by using std::move(), this is an essentially free operation:
	// The data is moved from the old array to the new one and the old array
	// is reset to zero size.
	std::vector<Item> oldItems = std::move(items);

	// Resize the cleared array to the new size. This leaves the checked
	// flag in an undefined state (since we didn't define a default constructor).
	items.resize(newList.count());

	// First, reset all checked states to false
	anyChecked = false;
	for (Item &item: items)
		item.checked = false;

	// Then, restore the checked state.  Ignore the last item, since
	// this is the "Show Empty Tags" entry.
	for (int i = 0; i < (int)oldItems.size() - 1; i++) {
		if (oldItems[i].checked) {
			int ind = newList.indexOf(stringList()[i]);
			if (ind >= 0 && ind < newList.count() - 1) {
				items[ind].checked = true;
				anyChecked = true;
			}
		}
	}

	// Reset the state of the "Show Empty Tags" entry. But be careful:
	// on program startup, the old list is empty.
	if (!oldItems.empty() && !items.empty() && oldItems.back().checked) {
		items.back().checked = true;
		anyChecked = true;
	}

	// Finally, calculate and cache the counts. Ignore the last item, since
	// this is the "Show Empty Tags" entry.
	for (int i = 0; i < (int)newList.size() - 1; i++)
		items[i].count = countDives(qPrintable(newList[i]));

	// Calculate count of "Empty Tags".
	if (!items.empty())
		items.back().count = countDives("");

	setStringList(newList);
}

Qt::ItemFlags FilterModelBase::flags(const QModelIndex &index) const
{
	return QStringListModel::flags(index) | Qt::ItemIsUserCheckable;
}

bool FilterModelBase::setData(const QModelIndex &index, const QVariant &value, int role)
{
	if (role == Qt::CheckStateRole) {
		items[index.row()].checked = value.toBool();
		anyChecked = false;
		for (const Item &item: items) {
			if (item.checked) {
				anyChecked = true;
				break;
			}
		}
		dataChanged(index, index);
		return true;
	}
	return false;
}

QVariant FilterModelBase::data(const QModelIndex &index, int role) const
{
	if (role == Qt::CheckStateRole) {
		return items[index.row()].checked ? Qt::Checked : Qt::Unchecked;
	} else if (role == Qt::DisplayRole) {
		int row = index.row();
		return QStringLiteral("%1 (%2)").arg(stringList()[row], QString::number(items[row].count));
	}
	return QVariant();
}

void FilterModelBase::clearFilter()
{
	for (Item &item: items)
		item.checked = false;
	anyChecked = false;
	emit dataChanged(createIndex(0, 0), createIndex(rowCount() - 1, 0));
}

void FilterModelBase::selectAll()
{
	for (Item &item: items)
		item.checked = true;
	anyChecked = true;
	emit dataChanged(createIndex(0, 0), createIndex(rowCount() - 1, 0));
}

void FilterModelBase::invertSelection()
{
	for (Item &item: items)
		item.checked = !item.checked;
	anyChecked = std::any_of(items.begin(), items.end(), [](Item &item) { return !!item.checked; });
	emit dataChanged(createIndex(0, 0), createIndex(rowCount() - 1, 0));
}

void FilterModelBase::setNegate(bool negateParam)
{
	negate = negateParam;
	emit dataChanged(createIndex(0, 0), createIndex(rowCount() - 1, 0));
}

SuitsFilterModel::SuitsFilterModel(QObject *parent) : FilterModelBase(parent)
{
}

int SuitsFilterModel::countDives(const char *s) const
{
	return count_dives_with_suit(s);
}

bool SuitsFilterModel::doFilter(const dive *d) const
{
	// rowCount() == 0 should never happen, because we have the "no suits" row
	// let's handle it gracefully anyway.
	if (!anyChecked || rowCount() == 0)
		return true;

	// Checked means 'Show', Unchecked means 'Hide'.
	QString suit(d->suit);
	// only show empty suit dives if the user checked that.
	if (suit.isEmpty())
		return items[rowCount() - 1].checked != negate;

	// there is a suit selected
	QStringList suitList = stringList();
	// Ignore last item, since this is the "Show Empty Tags" entry
	for (int i = 0; i < rowCount() - 1; i++) {
		if (items[i].checked && suit == suitList[i])
			return !negate;
	}
	return negate;
}

void SuitsFilterModel::repopulate()
{
	QStringList list;
	struct dive *dive;
	int i = 0;
	for_each_dive (i, dive) {
		QString suit(dive->suit);
		if (!suit.isEmpty() && !list.contains(suit)) {
			list.append(suit);
		}
	}
	qSort(list);
	list << tr("No suit set");
	updateList(list);
}

TagFilterModel::TagFilterModel(QObject *parent) : FilterModelBase(parent)
{
}

int TagFilterModel::countDives(const char *s) const
{
	return count_dives_with_tag(s);
}

void TagFilterModel::repopulate()
{
	if (g_tag_list == NULL)
		return;
	QStringList list;
	struct tag_entry *current_tag_entry = g_tag_list;
	while (current_tag_entry != NULL) {
		if (count_dives_with_tag(current_tag_entry->tag->name) > 0)
			list.append(QString(current_tag_entry->tag->name));
		current_tag_entry = current_tag_entry->next;
	}
	qSort(list);
	list << tr("Empty tags");
	updateList(list);
}

bool TagFilterModel::doFilter(const dive *d) const
{
	// If there's nothing checked, this should show everything
	// rowCount() == 0 should never happen, because we have the "no tags" row
	// let's handle it gracefully anyway.
	if (!anyChecked || rowCount() == 0)
		return true;

	// Checked means 'Show', Unchecked means 'Hide'.
	struct tag_entry *head = d->tag_list;

	if (!head) // last tag means "Show empty tags";
		return items[rowCount() - 1].checked != negate;

	// have at least one tag.
	QStringList tagList = stringList();
	if (!tagList.isEmpty()) {
		tagList.removeLast(); // remove the "Show Empty Tags";
		while (head) {
			QString tagName(head->tag->name);
			int index = tagList.indexOf(tagName);
			if (index >= 0 && items[index].checked)
				return !negate;
			head = head->next;
		}
	}
	return negate;
}

BuddyFilterModel::BuddyFilterModel(QObject *parent) : FilterModelBase(parent)
{
}

int BuddyFilterModel::countDives(const char *s) const
{
	return count_dives_with_person(s);
}

bool BuddyFilterModel::doFilter(const dive *d) const
{
	// If there's nothing checked, this should show everything
	// rowCount() == 0 should never happen, because we have the "no tags" row
	// let's handle it gracefully anyway.
	if (!anyChecked || rowCount() == 0)
		return true;

	// Checked means 'Show', Unchecked means 'Hide'.
	QString persons = QString(d->buddy) + "," + QString(d->divemaster);
	QStringList personsList = persons.split(',', QString::SkipEmptyParts);
	for (QString &s: personsList)
		s = s.trimmed();
	// only show empty buddie dives if the user checked that.
	if (personsList.isEmpty())
		return items[rowCount() - 1].checked != negate;

	// have at least one buddy
	QStringList buddyList = stringList();
	// Ignore last item, since this is the "Show Empty Tags" entry
	for (int i = 0; i < rowCount() - 1; i++) {
		if (items[i].checked && personsList.contains(buddyList[i], Qt::CaseInsensitive))
			return !negate;
	}
	return negate;
}

void BuddyFilterModel::repopulate()
{
	QStringList list;
	struct dive *dive;
	int i = 0;
	for_each_dive (i, dive) {
		QString persons = QString(dive->buddy) + "," + QString(dive->divemaster);
		Q_FOREACH (const QString &person, persons.split(',', QString::SkipEmptyParts)) {
			// Remove any leading spaces
			if (!list.contains(person.trimmed())) {
				list.append(person.trimmed());
			}
		}
	}
	qSort(list);
	list << tr("No buddies");
	updateList(list);
}

LocationFilterModel::LocationFilterModel(QObject *parent) : FilterModelBase(parent)
{
}

int LocationFilterModel::countDives(const char *s) const
{
	return count_dives_with_location(s);
}

bool LocationFilterModel::doFilter(const dive *d) const
{
	// rowCount() == 0 should never happen, because we have the "no location" row
	// let's handle it gracefully anyway.
	if (!anyChecked || rowCount() == 0)
		return true;

	// Checked means 'Show', Unchecked means 'Hide'.
	QString location(get_dive_location(d));
	// only show empty location dives if the user checked that.
	if (location.isEmpty())
		return items[rowCount() - 1].checked != negate;

	// There is a location selected
	QStringList locationList = stringList();
	// Ignore last item, since this is the "Show Empty Tags" entry
	for (int i = 0; i < rowCount() - 1; i++) {
		if (items[i].checked && location == locationList[i])
			return !negate;
	}
	return negate;
}

void LocationFilterModel::repopulate()
{
	QStringList list;
	struct dive *dive;
	int i = 0;
	for_each_dive (i, dive) {
		QString location(get_dive_location(dive));
		if (!location.isEmpty() && !list.contains(location)) {
			list.append(location);
		}
	}
	qSort(list);
	list << tr("No location set");
	updateList(list);
}

void LocationFilterModel::changeName(const QString &oldName, const QString &newName)
{
	if (oldName.isEmpty() || newName.isEmpty() || oldName == newName)
		return;
	QStringList list = stringList();
	int oldIndex = list.indexOf(oldName);
	if (oldIndex < 0)
		return;
	int newIndex = list.indexOf(newName);
	list[oldIndex] = newName;

	// If there was already an entry with the new name, we are merging entries.
	// Thus, if the old entry was selected, also select the new entry.
	if (newIndex >= 0 && items[oldIndex].checked)
		items[newIndex].checked = true;
	setStringList(list);
}

void LocationFilterModel::addName(const QString &newName)
{
	// If any item is checked and a new location is added, add the name
	// of the new location in front of the list and mark it as checked.
	// Thus, on subsequent repopulation of the list, the new entry will
	// be registered as already checked.
	QStringList list = stringList();
	if (!anyChecked || newName.isEmpty() || list.indexOf(newName) >= 0)
		return;
	list.prepend(newName);
	items.insert(items.begin(), { true });
	setStringList(list);
}

MultiFilterSortModel::MultiFilterSortModel(QObject *parent) : QSortFilterProxyModel(parent),
	divesDisplayed(0),
	curr_dive_site(NULL)
{
}

bool MultiFilterSortModel::showDive(const struct dive *d) const
{
	if (curr_dive_site) {
		dive_site *ds = get_dive_site_by_uuid(d->dive_site_uuid);
		if (!ds)
			return false;
		return same_string(ds->name, curr_dive_site->name) || ds->uuid == curr_dive_site->uuid;
	}

	if (models.isEmpty())
		return true;

	for (const FilterModelBase *model: models) {
		if (!model->doFilter(d))
			return false;
	}

	return true;
}

bool MultiFilterSortModel::filterAcceptsRow(int source_row, const QModelIndex &source_parent) const
{
	QModelIndex index0 = sourceModel()->index(source_row, 0, source_parent);
	QVariant diveVariant = sourceModel()->data(index0, DiveTripModel::DIVE_ROLE);
	if (!diveVariant.isValid())
		return true;
	struct dive *d = (struct dive *)diveVariant.value<void *>();

	// For dives, simply check the hidden_by_filter flag
	if (d)
		return !d->hidden_by_filter;

	// Since this is not a dive, it must be a trip
	QVariant tripVariant = sourceModel()->data(index0, DiveTripModel::TRIP_ROLE);
	dive_trip *trip = (dive_trip *)tripVariant.value<void *>();

	if (!trip)
		return false; // Oops. Neither dive nor trip, something is seriously wrong.

	// Show the trip if any dive is visible
	for (d = trip->dives; d; d = d->next) {
		if (!d->hidden_by_filter)
			return true;
	}
	return false;
}

void MultiFilterSortModel::myInvalidate()
{
#if !defined(SUBSURFACE_MOBILE)
	int i;
	struct dive *d;
	DiveListView *dlv = MainWindow::instance()->dive_list();

	divesDisplayed = 0;

	// Apply filter for each dive
	for_each_dive (i, d) {
		bool show = showDive(d);
		filter_dive(d, show);
		if (show)
			divesDisplayed++;
	}

	invalidateFilter();

	// first make sure the trips are no longer shown as selected
	// (but without updating the selection state of the dives... this just cleans
	//  up an oddity in the filter handling)
	// TODO: This should go internally to DiveList, to be triggered after a filter is due.
	dlv->clearTripSelection();

	// if we have no more selected dives, clean up the display - this later triggers us
	// to pick one of the dives that are shown in the list as selected dive which is the
	// natural behavior
	if (amount_selected == 0) {
		MainWindow::instance()->cleanUpEmpty();
	} else {
		// otherwise find the dives that should still be selected (the filter above unselected any
		// dive that's no longer visible) and select them again
		QList<int> curSelectedDives;
		for_each_dive (i, d) {
			if (d->selected)
				curSelectedDives.append(get_divenr(d));
		}
		dlv->selectDives(curSelectedDives);
	}

	emit filterFinished();

	if (curr_dive_site) {
		dlv->expandAll();
	}
#endif
}

void MultiFilterSortModel::addFilterModel(FilterModelBase *model)
{
	models.append(model);
	connect(model, SIGNAL(dataChanged(QModelIndex, QModelIndex)), this, SLOT(myInvalidate()));
}

void MultiFilterSortModel::removeFilterModel(FilterModelBase *model)
{
	models.removeAll(model);
	disconnect(model, SIGNAL(dataChanged(QModelIndex, QModelIndex)), this, SLOT(myInvalidate()));
}

void MultiFilterSortModel::clearFilter()
{
	Q_FOREACH (FilterModelBase *iface, models) {
		iface->clearFilter();
	}
	myInvalidate();
}

void MultiFilterSortModel::startFilterDiveSite(uint32_t uuid)
{
	curr_dive_site = get_dive_site_by_uuid(uuid);
	myInvalidate();
}

void MultiFilterSortModel::stopFilterDiveSite()
{
	curr_dive_site = NULL;
	myInvalidate();
}
