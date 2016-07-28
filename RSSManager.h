
#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "Util.h"
#include "Singleton.h"
#include "Speaker.h"
#include "SimpleXML.h"
#include "HttpDownload.h"
#include "TimerManager.h"
#include "ShareManager.h"
#include "AutoSearchManager.h"
#include "TargetUtil.h"

namespace dcpp {

class RSS : private boost::noncopyable {
public:

	RSS(const string& aUrl, const string& aCategory, time_t aLastUpdate, int aUpdateInterval = 30) noexcept :
		url(aUrl), category(aCategory), lastUpdate(aLastUpdate), updateInterval(aUpdateInterval)
	{
		if (aUpdateInterval < 10)
			updateInterval = 10;

		rssDownload.reset();
	}

	~RSS() noexcept {};

	GETSET(string, url, Url);
	GETSET(string, category, Category);
	GETSET(time_t, lastUpdate, LastUpdate);
	GETSET(int, updateInterval, UpdateInterval);

	//bool operator==(const RSSPtr& rhs) const { return url == rhs->getUrl(); }

	unordered_map<string, RSSDataPtr>& getFeedData() { return rssData; }

	unique_ptr<HttpDownload> rssDownload;

	bool allowUpdate() {
		return (getLastUpdate() + getUpdateInterval() * 60) < GET_TIME();
	}

private:

	unordered_map<string, RSSDataPtr> rssData;

};

class RSSData: public intrusive_ptr_base<RSSData>, private boost::noncopyable {
public:
	RSSData(const string& aTitle, const string& aLink, const string& aPubDate, const RSSPtr& aFeed, time_t aDateAdded = GET_TIME()) noexcept :
		title(aTitle), link(aLink), pubDate(aPubDate), feed(aFeed), dateAdded(aDateAdded)  {
	}
	~RSSData() noexcept { };
	
	GETSET(string, title, Title);
	GETSET(string, link, Link);
	GETSET(string, pubDate, PubDate);
	GETSET(RSSPtr, feed, Feed);
	GETSET(time_t, dateAdded, DateAdded); //For prune old entries in database...

};


class RSSFilter {
public:

	RSSFilter(const string& aFilterPattern, const string& aDownloadTarget) noexcept :
		filterPattern(aFilterPattern), downloadTarget(aDownloadTarget)
	{
	}

	~RSSFilter() noexcept {};

	GETSET(string, filterPattern, FilterPattern);
	GETSET(string, downloadTarget, DownloadTarget);

};

class RSSManagerListener {
public:
	virtual ~RSSManagerListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> RSSDataAdded;
	typedef X<1> RSSDataCleared;
	typedef X<2> RSSFeedUpdated;
	typedef X<3> RSSFeedChanged;
	typedef X<4> RSSFeedRemoved;
	typedef X<5> RSSFeedAdded;

	virtual void on(RSSDataAdded, const RSSDataPtr&) noexcept { }
	virtual void on(RSSDataCleared, const RSSPtr&) noexcept { }
	virtual void on(RSSFeedUpdated, const RSSPtr&) noexcept { }
	virtual void on(RSSFeedChanged, const RSSPtr&) noexcept { }
	virtual void on(RSSFeedRemoved, const RSSPtr&) noexcept { }
	virtual void on(RSSFeedAdded, const RSSPtr&) noexcept { }

};


class RSSManager : public Speaker<RSSManagerListener>, public Singleton<RSSManager>, private TimerManagerListener
{
public:
	friend class Singleton<RSSManager>;	
	RSSManager();
	~RSSManager();

	void load();
	void save();

	void clearRSSData(const RSSPtr& aFeed);
	void matchFilters(const RSSPtr& aFeed);
	
	RSSPtr getFeedByCategory(const string& aCategory);
	RSSPtr getFeedByUrl(const string& aUrl);

	CriticalSection& getCS() { return cs; }

	unordered_set<RSSPtr>& getRss(){
		return rssList;
	}

	vector<RSSFilter>& getRssFilterList() {
		return rssFilterList;
	}

	void downloadFeed(const RSSPtr& aFeed);

	void updateFeedItem(RSSPtr& aFeed, const string& aUrl, const string& aCategory, int aUpdateInterval);
	
	void updateFilterList(vector<RSSFilter>& aNewList);

	void removeFeedItem(const RSSPtr& aFeed);

private:

	void loaddatabase(const RSSPtr& aFeed, SimpleXML& aXml);
	void savedatabase(const RSSPtr& aFeed, SimpleXML& aXml);

	uint64_t nextUpdate;

	RSSPtr getUpdateItem();
	
	void matchFilters(const RSSDataPtr& aData);

	unordered_set<RSSPtr> rssList;

	vector<RSSFilter> rssFilterList;
	
	mutable CriticalSection cs;

	void downloadComplete(const string& aUrl);
	// TimerManagerListener
	void on(TimerManagerListener::Second, uint64_t tick) noexcept;

};

}