// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.bookmarks;

import org.chromium.base.Callback;
import org.chromium.base.ObserverList;
import org.chromium.base.VisibleForTesting;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.chrome.browser.offlinepages.ClientId;
import org.chromium.chrome.browser.offlinepages.OfflinePageBridge;
import org.chromium.chrome.browser.offlinepages.OfflinePageBridge.OfflinePageModelObserver;
import org.chromium.chrome.browser.offlinepages.OfflinePageBridge.SavePageCallback;
import org.chromium.chrome.browser.offlinepages.OfflinePageItem;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.components.bookmarks.BookmarkId;
import org.chromium.components.bookmarks.BookmarkType;
import org.chromium.components.dom_distiller.core.DomDistillerUrlUtils;
import org.chromium.components.offlinepages.SavePageResult;
import org.chromium.content_public.browser.WebContents;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;

/**
 * A class that encapsulates {@link BookmarkBridge} and provides extra features such as undo, large
 * icon fetching, reader mode url redirecting, etc. This class should serve as the single class for
 * the UI to acquire data from the backend.
 */
public class BookmarkModel extends BookmarkBridge {
    private static final int FAVICON_MAX_CACHE_SIZE = 10 * 1024 * 1024; // 10MB

    /**
     * Callback for use with addBookmarkAsync / saveOfflinePage.
     */
    public interface AddBookmarkCallback {
        static final int SAVED = 0;
        static final int SKIPPED = 1;
        static final int ERROR = 2;

        /**
         * Called when the bookmark has been added.
         * @param bookmarkId ID of the bookmark that has been added.
         * @param result of saving an offline copy of the bookmarked page.
         */
        void onBookmarkAdded(BookmarkId bookmarkId, int saveResult);
    }

    /**
     * Observer that listens to delete event. This interface is used by undo controllers to know
     * which bookmarks were deleted. Note this observer only listens to events that go through
     * bookmark model.
     */
    public interface BookmarkDeleteObserver {

        /**
         * Callback being triggered immediately before bookmarks are deleted.
         * @param titles All titles of the bookmarks to be deleted.
         * @param isUndoable Whether the deletion is undoable.
         */
        void onDeleteBookmarks(String[] titles, boolean isUndoable);
    }

    /** A comparator to sort the offline pages according to the most recent access time. */
    private static final Comparator<OfflinePageItem> sOfflinePageComparator =
            new Comparator<OfflinePageItem>() {
                @Override
                public int compare(OfflinePageItem o1, OfflinePageItem o2) {
                    if (o1.getLastAccessTimeMs() > o2.getLastAccessTimeMs()) {
                        return -1;
                    } else if (o1.getLastAccessTimeMs() < o2.getLastAccessTimeMs()) {
                        return 1;
                    } else {
                        return 0;
                    }
                }
            };

    private ObserverList<BookmarkDeleteObserver> mDeleteObservers = new ObserverList<>();
    private OfflinePageBridge mOfflinePageBridge;
    private boolean mIsOfflinePageModelLoaded;
    private OfflinePageModelObserver mOfflinePageModelObserver;

    /**
     * Initialize bookmark model for last used non-incognito profile.
     */
    public BookmarkModel() {
        this(Profile.getLastUsedProfile().getOriginalProfile());
    }

    @VisibleForTesting
    public BookmarkModel(Profile profile) {
        super(profile);

        // Note: we check if mOfflinePageBridge is null after this to determine if offline pages
        // feature is enabled. When it is enabled by default, we should check all the places
        // that checks for nullability of mOfflinePageBridge.
        if (OfflinePageBridge.isEnabled()) {
            mOfflinePageBridge = OfflinePageBridge.getForProfile(profile);
            if (mOfflinePageBridge.isOfflinePageModelLoaded()) {
                mIsOfflinePageModelLoaded = true;
            } else {
                mOfflinePageModelObserver = new OfflinePageModelObserver() {
                    @Override
                    public void offlinePageModelLoaded() {
                        mIsOfflinePageModelLoaded = true;
                        if (isBookmarkModelLoaded()) {
                            notifyBookmarkModelLoaded();
                        }
                    }
                };
                mOfflinePageBridge.addObserver(mOfflinePageModelObserver);
            }
        }
    }

    /**
     * Clean up all the bridges. This must be called after done using this class.
     */
    @Override
    public void destroy() {
        if (mOfflinePageBridge != null) {
            mOfflinePageBridge.removeObserver(mOfflinePageModelObserver);
            mOfflinePageBridge = null;
        }

        super.destroy();
    }

    @Override
    public boolean isBookmarkModelLoaded() {
        return super.isBookmarkModelLoaded()
                && (mOfflinePageBridge == null || mIsOfflinePageModelLoaded);
    }

    /**
     * Add an observer that listens to delete events that go through the bookmark model.
     * @param observer The observer to add.
     */
    public void addDeleteObserver(BookmarkDeleteObserver observer) {
        mDeleteObservers.addObserver(observer);
    }

    /**
     * Remove the observer from listening to bookmark deleting events.
     * @param observer The observer to remove.
     */
    public void removeDeleteObserver(BookmarkDeleteObserver observer) {
        mDeleteObservers.removeObserver(observer);
    }

    /**
     * Delete one or multiple bookmarks from model. If more than one bookmarks are passed here, this
     * method will group these delete operations into one undo bundle so that later if the user
     * clicks undo, all bookmarks deleted here will be restored.
     * @param bookmarks Bookmarks to delete. Note this array should not contain a folder and its
     *                  children, because deleting folder will also remove all its children, and
     *                  deleting children once more will cause errors.
     */
    public void deleteBookmarks(BookmarkId... bookmarks) {
        assert bookmarks != null && bookmarks.length > 0;
        // Store all titles of bookmarks.
        String[] titles = new String[bookmarks.length];
        boolean isUndoable = true;
        for (int i = 0; i < bookmarks.length; i++) {
            titles[i] = getBookmarkTitle(bookmarks[i]);
            isUndoable &= (bookmarks[i].getType() == BookmarkType.NORMAL);
        }

        if (bookmarks.length == 1) {
            deleteBookmark(bookmarks[0]);
        } else {
            startGroupingUndos();
            for (BookmarkId bookmark : bookmarks) {
                deleteBookmark(bookmark);
            }
            endGroupingUndos();
        }

        for (BookmarkDeleteObserver observer : mDeleteObservers) {
            observer.onDeleteBookmarks(titles, isUndoable);
        }
    }

    /**
     * Calls {@link BookmarkBridge#moveBookmark(BookmarkId, BookmarkId, int)} for the given
     * bookmark list. The bookmarks are appended at the end.
     */
    public void moveBookmarks(List<BookmarkId> bookmarkIds, BookmarkId newParentId) {
        int appenedIndex = getChildCount(newParentId);
        for (int i = 0; i < bookmarkIds.size(); ++i) {
            moveBookmark(bookmarkIds.get(i), newParentId, appenedIndex + i);
        }
    }

    /**
     * Add a new bookmark asynchronously.
     *
     * @param parent Folder where to add.
     * @param index The position where the bookmark will be placed in parent folder
     * @param title Title of the new bookmark.
     * @param url Url of the new bookmark
     * @param webContents A {@link WebContents} object for saving the offline copy. If null, the
     *        saving should be skipped.
     * @param callback The callback to be invoked when the bookmark is added.
     */
    public void addBookmarkAsync(BookmarkId parent, int index, String title, String url,
                                 WebContents webContents, final AddBookmarkCallback callback) {
        url = DomDistillerUrlUtils.getOriginalUrlFromDistillerUrl(url);
        final BookmarkId bookmarkId = addBookmark(parent, index, title, url);

        // If bookmark was not created return an error.
        if (bookmarkId == null) {
            callback.onBookmarkAdded(null, AddBookmarkCallback.ERROR);
            return;
        }

        // If there is no need to save offline page, return now.
        if (mOfflinePageBridge == null || webContents == null) {
            callback.onBookmarkAdded(bookmarkId, AddBookmarkCallback.SKIPPED);
            return;
        }

        saveOfflinePage(bookmarkId, webContents, callback);
    }

    /**
    * Save an offline copy for the bookmarked page asynchronously.
    *
    * @param bookmarkId The ID of the page to save an offline copy.
    * @param webContents A {@link WebContents} object.
    * @param callback The callback to be invoked when the offline copy is saved.
    */
    public void saveOfflinePage(final BookmarkId bookmarkId, WebContents webContents,
            final AddBookmarkCallback callback) {
        if (mOfflinePageBridge != null) {
            RecordHistogram.recordBooleanHistogram("OfflinePages.IncognitoSave",
                    webContents.isIncognito());
            ClientId clientId = ClientId.createClientIdForBookmarkId(bookmarkId);
            mOfflinePageBridge.savePage(webContents, clientId, new SavePageCallback() {
                @Override
                public void onSavePageDone(int savePageResult, String url, long offlineId) {
                    int saveResult;
                    if (savePageResult == SavePageResult.SUCCESS) {
                        saveResult = AddBookmarkCallback.SAVED;
                    } else if (savePageResult == SavePageResult.SKIPPED) {
                        saveResult = AddBookmarkCallback.SKIPPED;
                    } else {
                        saveResult = AddBookmarkCallback.ERROR;
                    }
                    callback.onBookmarkAdded(bookmarkId, saveResult);
                }
            });
        }
    }

    /**
     * @see org.chromium.chrome.browser.bookmarks.BookmarkBridge.BookmarkItem#getTitle()
     */
    public String getBookmarkTitle(BookmarkId bookmarkId) {
        return getBookmarkById(bookmarkId).getTitle();
    }

    /**
     * Retrieves the url to launch a bookmark or saved page. If latter, also marks it as being
     * accessed and reports the UMAs.
     *
     * @param bookmarkId ID of the bookmark to launch.
     * @return The launch URL.
     */
    public String getLaunchUrlAndMarkAccessed(BookmarkId bookmarkId) {
        String url = getBookmarkById(bookmarkId).getUrl();
        if (mOfflinePageBridge == null) return url;

        return mOfflinePageBridge.getLaunchUrlFromOnlineUrl(url);
    }

    /**
     * @return The id of the default folder to add bookmarks/folders to.
     */
    public BookmarkId getDefaultFolder() {
        return getMobileFolderId();
    }

    /**
     * Gets a list of bookmark IDs for all offline pages.
     *
     * @return A list of bookmark IDs of bookmarks matching the offline pages filter.
     */
    public void getBookmarkIDsByFilter(
            BookmarkFilter filter, final Callback<List<BookmarkId>> callback) {
        assert filter == BookmarkFilter.OFFLINE_PAGES;
        assert mOfflinePageBridge != null;

        mOfflinePageBridge.getAllPages(new OfflinePageBridge.MultipleOfflinePageItemCallback() {
            @Override
            public void onResult(List<OfflinePageItem> offlinePages) {
                callback.onResult(filterBookmarkIdsByOfflinePages(offlinePages));
            }
        });
    }

    /**
     * Gets all bookmarks that correspond to the given list of offline pages, in MRU order.
     * @see http://crbug.com/537806
     */
    private List<BookmarkId> filterBookmarkIdsByOfflinePages(List<OfflinePageItem> offlinePages) {
        Collections.sort(offlinePages, sOfflinePageComparator);

        HashSet<BookmarkId> existingBookmarks =
                new HashSet<>(getAllBookmarkIDsOrderedByCreationDate());

        List<BookmarkId> bookmarkIds = new ArrayList<>();
        for (OfflinePageItem offlinePage : offlinePages) {
            BookmarkId bookmarkId = getBookmarkIdForOfflineClientId(offlinePage.getClientId());
            if (existingBookmarks.contains(bookmarkId)) {
                bookmarkIds.add(bookmarkId);
            }
        }
        return bookmarkIds;
    }

    /**
     * @return Offline page bridge.
     */
    public OfflinePageBridge getOfflinePageBridge() {
        return mOfflinePageBridge;
    }

    /**
     * @param id The client id to convert.
     * @return The bookmark id contained in the specified client id.
     */
    public static BookmarkId getBookmarkIdForOfflineClientId(ClientId id) {
        if (!id.getNamespace().equals(OfflinePageBridge.BOOKMARK_NAMESPACE)) {
            return new BookmarkId(-1, BookmarkType.NORMAL);
        }
        return BookmarkId.getBookmarkIdFromString(id.getId());
    }
}
