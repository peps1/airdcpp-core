/*
 * Copyright (C) 2011 AirDC++ Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "stdinc.h"

#include <boost/range/algorithm/for_each.hpp>
#include <boost/range/algorithm_ext/for_each.hpp>
#include <boost/fusion/algorithm/iteration/accumulate.hpp>
#include <boost/random/discrete_distribution.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <boost/fusion/include/count_if.hpp>

#include "BundleQueue.h"
#include "SettingsManager.h"
#include "AirUtil.h"
#include "QueueItem.h"
#include "LogManager.h"
#include "TimerManager.h"

namespace dcpp {

using boost::range::for_each;
using boost::fusion::accumulate;

BundleQueue::BundleQueue() : 
	nextSearch(0),
	nextRecentSearch(0)
{ 
	highestSel=0, highSel=0, normalSel=0, lowSel=0, calculations=0;
}

BundleQueue::~BundleQueue() { }

void BundleQueue::add(BundlePtr aBundle) {
	aBundle->unsetFlag(Bundle::FLAG_NEW);
	aBundle->setDownloadedBytes(0); //sets to downloaded segments

	addSearchPrio(aBundle);
	bundles[aBundle->getToken()] = aBundle;

	//check if we need to insert the root bundle dir
	if (!aBundle->isFileBundle()) {
		if (aBundle->getBundleDirs().find(aBundle->getTarget()) == aBundle->getBundleDirs().end()) {
			string releaseDir = AirUtil::getReleaseDir(aBundle->getTarget());
			if (!releaseDir.empty()) {
				bundleDirs[releaseDir] = aBundle;
			}
		}
	}
}

void BundleQueue::addSearchPrio(BundlePtr aBundle) {
	if (aBundle->getPriority() < Bundle::LOW) {
		return;
	}

	if (aBundle->isRecent()) {
		dcassert(std::find(recentSearchQueue.begin(), recentSearchQueue.end(), aBundle) == recentSearchQueue.end());
		recentSearchQueue.push_back(aBundle);
		return;
	} else {
		dcassert(std::find(prioSearchQueue[aBundle->getPriority()].begin(), prioSearchQueue[aBundle->getPriority()].end(), aBundle) == prioSearchQueue[aBundle->getPriority()].end());
		prioSearchQueue[aBundle->getPriority()].push_back(aBundle);
	}
}

void BundleQueue::removeSearchPrio(BundlePtr aBundle) {
	if (aBundle->getPriority() < Bundle::LOW) {
		return;
	}

	if (aBundle->isRecent()) {
		auto i = std::find(recentSearchQueue.begin(), recentSearchQueue.end(), aBundle);
		dcassert(i != recentSearchQueue.end());
		if (i != recentSearchQueue.end()) {
			recentSearchQueue.erase(i);
		}
	} else {
		auto i = std::find(prioSearchQueue[aBundle->getPriority()].begin(), prioSearchQueue[aBundle->getPriority()].end(), aBundle);
		dcassert(i != prioSearchQueue[aBundle->getPriority()].end());
		if (i != prioSearchQueue[aBundle->getPriority()].end()) {
			prioSearchQueue[aBundle->getPriority()].erase(i);
		}
	}
}

BundlePtr BundleQueue::findRecent() {
	if ((int)recentSearchQueue.size() == 0) {
		return NULL;
	}
	BundlePtr tmp = recentSearchQueue.front();
	recentSearchQueue.pop_front();
	//check if the bundle still belongs to here
	if (tmp->checkRecent()) {
		//LogManager::getInstance()->message("Time remaining as recent: " + Util::toString(((tmp->getDirDate() + (SETTING(RECENT_BUNDLE_HOURS)*60*60)) - GET_TIME()) / (60)) + " minutes");
		recentSearchQueue.push_back(tmp);
	} else {
		//LogManager::getInstance()->message("REMOVE RECENT");
		addSearchPrio(tmp);
	}
	return tmp;
}

boost::mt19937 gen;
static vector<double> probabilities;

int BundleQueue::getPrioSum() {
	probabilities.clear();

	int prioBundles = 0;
	int p = Bundle::LOW ;
	do {
		int dequeBundles = count_if(prioSearchQueue[p].begin(), prioSearchQueue[p].end(), [&](BundlePtr b) {
			return b->allowAutoSearch();
		});
		probabilities.push_back((int)(p-1)*dequeBundles);
		prioBundles += dequeBundles;
		p++;
	} while(p < Bundle::LAST);

	probabilities.shrink_to_fit();
	return prioBundles;
}

BundlePtr BundleQueue::findAutoSearch() {
	int prioBundles = getPrioSum();

	//do we have anything where to search from?
	if (prioBundles == 0) {
		return nullptr;
	}

	auto dist = boost::random::discrete_distribution<>(probabilities.begin(), probabilities.end());

	//choose the search queue, can't be paused or lowest
	auto& sbq = prioSearchQueue[dist(gen) + 2];
	dcassert(!sbq.empty());

	//find the first item that can be searched for
	auto s = find_if(sbq.begin(), sbq.end(), [&](BundlePtr b) { return b->allowAutoSearch(); } );

	if (s != sbq.end()) {
		BundlePtr b = *s;
		sbq.erase(s);
		sbq.push_back(b);
		return b;
	}

	return nullptr;
}

BundlePtr BundleQueue::find(const string& bundleToken) {
	auto i = bundles.find(bundleToken);
	if (i != bundles.end()) {
		return i->second;
	}
	return nullptr;
}

BundlePtr BundleQueue::findDir(const string& aPath) {
	string dir = AirUtil::getReleaseDir(aPath);
	if (dir.empty()) {
		return nullptr;
	}

	auto i = bundleDirs.find(dir);
	if (i != bundleDirs.end()) {
		return i->second;
	}
	return nullptr;
}

void BundleQueue::getInfo(const string& aSource, BundleList& retBundles, int& finishedFiles, int& fileBundles) {
	BundlePtr tmpBundle;
	bool subFolder = false;

	//find the matching bundles
	for (auto j = bundles.begin(); j != bundles.end(); ++j) {
		tmpBundle = j->second;
		if (tmpBundle->isFinished()) {
			//don't modify those
			continue;
		}

		if (AirUtil::isParent(aSource, tmpBundle->getTarget())) {
			//parent or the same dir
			retBundles.push_back(tmpBundle);
			if (tmpBundle->isFileBundle())
				fileBundles++;
		} else if (!tmpBundle->isFileBundle() && AirUtil::isSub(aSource, tmpBundle->getTarget())) {
			//subfolder
			retBundles.push_back(tmpBundle);
			subFolder = true;
			break;
		}
	}

	//count the finished files
	if (subFolder) {
		for_each(tmpBundle->getFinishedFiles(), [&](QueueItemPtr qi) { 
			if(AirUtil::isSub(qi->getTarget(), aSource)) 
				finishedFiles++; 
		});
	} else {
		for_each(retBundles, [&](BundlePtr b) { finishedFiles += b->getFinishedFiles().size(); });
	}
}

BundlePtr BundleQueue::getMergeBundle(const string& aTarget) {
	/* Returns directory bundles that are in sub or parent dirs (or in the same location), in which we can merge to */
	BundlePtr compareBundle;
	for (auto j = bundles.begin(); j != bundles.end(); ++j) {
		BundlePtr compareBundle = j->second;
		if (!compareBundle->isFileBundle() && (AirUtil::isSub(aTarget, compareBundle->getTarget()) || AirUtil::isParent(aTarget, compareBundle->getTarget()))) {
			return compareBundle;
		}
	}
	return NULL;
}

void BundleQueue::getSubBundles(const string& aTarget, BundleList& retBundles) {
	/* Returns bundles that are inside aTarget */
	for (auto j = bundles.begin(); j != bundles.end(); ++j) {
		BundlePtr compareBundle = j->second;
		if (AirUtil::isSub(compareBundle->getTarget(), aTarget)) {
			retBundles.push_back(compareBundle);
		}
	}
}

void BundleQueue::addBundleItem(QueueItemPtr qi, BundlePtr aBundle) {
	if (aBundle->addQueue(qi) && !aBundle->isFileBundle()) {
		string dir = Util::getDir(qi->getTarget(), false, false);
		string releaseDir = AirUtil::getReleaseDir(dir);
		if (!releaseDir.empty()) {
			bundleDirs[releaseDir] = aBundle;
		}
	}
}

void BundleQueue::removeBundleItem(QueueItemPtr qi, bool finished) {
	if (qi->getBundle()->removeQueue(qi, finished) && !finished && !qi->getBundle()->isFileBundle()) {
		string releaseDir = AirUtil::getReleaseDir(Util::getDir(qi->getTarget(), false, false));
		if (!releaseDir.empty()) {
			bundleDirs.erase(releaseDir);
		}
	}
}

void BundleQueue::addFinishedItem(QueueItemPtr qi, BundlePtr aBundle) {
	if (aBundle->addFinishedItem(qi, false) && !aBundle->isFileBundle()) {
		string dir = Util::getDir(qi->getTarget(), false, false);
		string releaseDir = AirUtil::getReleaseDir(dir);
		if (!releaseDir.empty()) {
			bundleDirs[releaseDir] = aBundle;
		}
	}
}

void BundleQueue::removeFinishedItem(QueueItemPtr qi) {
	if (qi->getBundle()->removeFinishedItem(qi) && !qi->getBundle()->isFileBundle()) {
		string releaseDir = AirUtil::getReleaseDir(Util::getDir(qi->getTarget(), false, false));
		if (!releaseDir.empty()) {
			bundleDirs.erase(releaseDir);
		}
	}
}

void BundleQueue::remove(BundlePtr aBundle) {
	for_each(aBundle->getBundleDirs(), [&](pair<string, pair<uint32_t, uint32_t>> dirs) {
		string releaseDir = AirUtil::getReleaseDir(dirs.first);
		if (!releaseDir.empty()) {
			bundleDirs.erase(releaseDir);
		}
	});

	//make sure that everything will be freed from the memory
	for(auto i = aBundle->getFinishedFiles().begin(); i != aBundle->getFinishedFiles().end(); )
		aBundle->getFinishedFiles().erase(i);
	for(auto i = aBundle->getQueueItems().begin(); i != aBundle->getQueueItems().end(); )
		aBundle->getQueueItems().erase(i);

	bundles.erase(aBundle->getToken());
}

void BundleQueue::move(BundlePtr aBundle, const string& newTarget) {
	//remove the old release dir
	string releaseDir = AirUtil::getReleaseDir(aBundle->getTarget());
	if (!releaseDir.empty()) {
		bundleDirs.erase(releaseDir);
	}

	aBundle->setTarget(newTarget);

	//add new
	releaseDir = AirUtil::getReleaseDir(aBundle->getTarget());
	if (!releaseDir.empty()) {
		bundleDirs[releaseDir] = aBundle;
	}
}

void BundleQueue::getAutoPrioMap(multimap<int, BundlePtr>& finalMap, int& uniqueValues) {
	//get bundles with auto priority
	boost::unordered_map<BundlePtr, double, Bundle::Hash> autoPrioMap;
	multimap<double, BundlePtr> sizeMap;
	multimap<int64_t, BundlePtr> sourceMap;
	{
		for (auto i = bundles.begin(); i != bundles.end(); ++i) {
			BundlePtr bundle = i->second;
			if (bundle->getAutoPriority() && !bundle->isFinished()) {
				auto p = bundle->getPrioInfo();
				sourceMap.insert(make_pair(p.first, bundle));
				sizeMap.insert(make_pair(p.second, bundle));
				autoPrioMap[bundle] = 0;
				/*if (verbose) {
					LogManager::getInstance()->message("Bundle " + bundle->getName() + ", time left " + Util::formatTime(p.first) + ", size factor " + Util::toString(p.second));
				} */
			}
		}
	}

	if (autoPrioMap.size() <= 1) {
		return;
	}

	//scale the priorization maps
	double factor;
	double max = max_element(sourceMap.begin(), sourceMap.end())->first;
	if (max) {
		double factor = 100 / max;
		for (auto i = sourceMap.begin(); i != sourceMap.end(); ++i) {
			autoPrioMap[i->second] = i->first * factor;
		}
	}

	max = max_element(sizeMap.begin(), sizeMap.end())->first;
	if (max > 0) {
		factor = 100 / max;
		for (auto i = sizeMap.begin(); i != sizeMap.end(); ++i) {
			autoPrioMap[i->second] += i->first * factor;
		}
	}

	{
		//prepare the finalmap
		for (auto i = autoPrioMap.begin(); i != autoPrioMap.end(); ++i) {
			if (finalMap.find(i->second) == finalMap.end()) {
				uniqueValues++;
			}
			finalMap.insert(make_pair(i->second, i->first));
		}
	}
}

BundlePtr BundleQueue::findSearchBundle(uint64_t aTick, bool force /* =false */) {
	BundlePtr bundle = NULL;
	if((BOOLSETTING(AUTO_SEARCH) && (aTick >= nextSearch) && (bundles.size() > 0)) || force) {
		bundle = findAutoSearch();
		//LogManager::getInstance()->message("Next search in " + Util::toString(next) + " minutes");
	} 
	
	if(!bundle && (BOOLSETTING(AUTO_SEARCH) && (aTick >= nextRecentSearch) || force)) {
		bundle = findRecent();
		//LogManager::getInstance()->message("Next recent search in " + Util::toString(recentBundles > 1 ? 5 : 10) + " minutes");
	}

	if(bundle) {
		if (!bundle->isRecent()) {
			calculations++;
			switch((int)bundle->getPriority()) {
				case 2:
					lowSel++;
					break;
				case 3:
					normalSel++;
					break;
				case 4:
					highSel++;
					break;
				case 5:
					highestSel++;
					break;
			}
		} else {
			//LogManager::getInstance()->message("Performing search for a RECENT bundle: " + bundle->getName());
		}
		//LogManager::getInstance()->message("Calculations performed: " + Util::toString(calculations) + ", highest: " + Util::toString(((double)highestSel/calculations)*100) + "%, high: " + Util::toString(((double)highSel/calculations)*100) + "%, normal: " + Util::toString(((double)normalSel/calculations)*100) + "%, low: " + Util::toString(((double)lowSel/calculations)*100) + "%");
	}
	return bundle;
}

int64_t BundleQueue::recalculateSearchTimes(BundlePtr aBundle, bool isPrioChange) {
	if (!aBundle->isRecent()) {
		int prioBundles = getPrioSum();
		int next = SETTING(SEARCH_TIME);
		if (prioBundles > 0) {
			next = max(60 / prioBundles, next);
		}
		if (nextSearch > 0 && isPrioChange) {
			nextSearch = min(nextSearch, GET_TICK() + (next * 60 * 1000));
		} else {
			nextSearch = GET_TICK() + (next * 60 * 1000);
		}
		return nextSearch;
	}
	
	if (nextRecentSearch > 0 && isPrioChange) {
		nextRecentSearch = min(nextRecentSearch, GET_TICK() + ((getRecentSize() > 1 ? 5 : 10) * 60 * 1000));
	} else {
		nextRecentSearch = GET_TICK() + ((getRecentSize() > 1 ? 5 : 10) * 60 * 1000);
	}
	return nextRecentSearch;
}

void BundleQueue::getDiskInfo(map<string, pair<string, int64_t>>& dirMap, const StringSet& volumes) {
	string tempVol;
	bool useSingleTempDir = (SETTING(TEMP_DOWNLOAD_DIRECTORY).find("%[targetdrive]") == string::npos);
	if (useSingleTempDir) {
		tempVol = AirUtil::getMountPath(SETTING(TEMP_DOWNLOAD_DIRECTORY), volumes);
	}

	for (auto i = bundles.begin(); i != bundles.end(); ++i) {
		BundlePtr b = (*i).second;
		string mountPath = AirUtil::getMountPath(b->getTarget(), volumes);
		if (!mountPath.empty()) {
			auto s = dirMap.find(mountPath);
			if (s != dirMap.end()) {
				bool countAll = (useSingleTempDir && (mountPath != tempVol));
				s->second.second -= b->getDiskUse(countAll);
			}
		}
	}
}

void BundleQueue::saveQueue(bool force) noexcept {
	try {
		for (auto i = bundles.begin(); i != bundles.end(); ++i) {
			BundlePtr b = i->second;
			if (!b->isFinished() && (b->getDirty() || force)) {
				b->save();
			}
		}
	} catch(...) {
		// ...
	}
}

} //dcpp