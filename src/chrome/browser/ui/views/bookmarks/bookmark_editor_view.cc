// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/bookmarks/bookmark_editor_view.h"

#include <string>

#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/bookmarks/bookmark_utils.h"
#include "chrome/browser/ui/bookmarks/bookmark_utils_desktop.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/grit/locale_settings.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_utils.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/history/core/browser/history_service.h"
#include "components/url_formatter/url_fixer.h"
#include "components/user_prefs/user_prefs.h"
#include "grit/components_strings.h"
#include "ui/accessibility/ax_view_state.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/events/event.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/tree/tree_view.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/layout/layout_constants.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_client_view.h"
#include "url/gurl.h"

using bookmarks::BookmarkExpandedStateTracker;
using bookmarks::BookmarkModel;
using bookmarks::BookmarkNode;
using views::GridLayout;

namespace {

// Background color of text field when URL is invalid.
const SkColor kErrorColor = SkColorSetRGB(0xFF, 0xBC, 0xBC);

}  // namespace

BookmarkEditorView::BookmarkEditorView(
    Profile* profile,
    const BookmarkNode* parent,
    const EditDetails& details,
    BookmarkEditor::Configuration configuration)
    : profile_(profile),
      tree_view_(NULL),
      url_label_(NULL),
      url_tf_(NULL),
      title_label_(NULL),
      title_tf_(NULL),
      parent_(parent),
      details_(details),
      bb_model_(BookmarkModelFactory::GetForProfile(profile)),
      running_menu_for_root_(false),
      show_tree_(configuration == SHOW_TREE) {
  DCHECK(profile);
  DCHECK(bb_model_);
  DCHECK(bb_model_->client()->CanBeEditedByUser(parent));
  Init();
}

BookmarkEditorView::~BookmarkEditorView() {
  // The tree model is deleted before the view. Reset the model otherwise the
  // tree will reference a deleted model.
  if (tree_view_)
    tree_view_->SetModel(NULL);
  bb_model_->RemoveObserver(this);
}

base::string16 BookmarkEditorView::GetDialogButtonLabel(
    ui::DialogButton button) const {
  if (button == ui::DIALOG_BUTTON_OK)
    return l10n_util::GetStringUTF16(IDS_SAVE);
  return views::DialogDelegateView::GetDialogButtonLabel(button);
}

bool BookmarkEditorView::IsDialogButtonEnabled(ui::DialogButton button) const {
  if (button == ui::DIALOG_BUTTON_OK) {
    if (!bb_model_->loaded())
      return false;

    if (details_.GetNodeType() != BookmarkNode::FOLDER)
      return GetInputURL().is_valid();
  }
  return true;
}

views::View* BookmarkEditorView::CreateExtraView() {
  return new_folder_button_.get();
}

ui::ModalType BookmarkEditorView::GetModalType() const {
  return ui::MODAL_TYPE_WINDOW;
}

bool BookmarkEditorView::CanResize() const {
  return true;
}

base::string16 BookmarkEditorView::GetWindowTitle() const {
  return l10n_util::GetStringUTF16(details_.GetWindowTitleId());
}

bool BookmarkEditorView::Accept() {
  if (!IsDialogButtonEnabled(ui::DIALOG_BUTTON_OK)) {
    if (details_.GetNodeType() != BookmarkNode::FOLDER) {
      // The url is invalid, focus the url field.
      url_tf_->SelectAll(true);
      url_tf_->RequestFocus();
    }
    return false;
  }
  // Otherwise save changes and close the dialog box.
  ApplyEdits();
  return true;
}

gfx::Size BookmarkEditorView::GetPreferredSize() const {
  if (!show_tree_)
    return views::View::GetPreferredSize();

  return gfx::Size(views::Widget::GetLocalizedContentsSize(
      IDS_EDITBOOKMARK_DIALOG_WIDTH_CHARS,
      IDS_EDITBOOKMARK_DIALOG_HEIGHT_LINES));
}

void BookmarkEditorView::OnTreeViewSelectionChanged(
    views::TreeView* tree_view) {
}

bool BookmarkEditorView::CanEdit(views::TreeView* tree_view,
                                 ui::TreeModelNode* node) {
  // Only allow editting of children of the bookmark bar node and other node.
  EditorNode* bb_node = tree_model_->AsNode(node);
  return (bb_node->parent() && bb_node->parent()->parent());
}

void BookmarkEditorView::ContentsChanged(views::Textfield* sender,
                                         const base::string16& new_contents) {
  UserInputChanged();
}

bool BookmarkEditorView::HandleKeyEvent(views::Textfield* sender,
                                        const ui::KeyEvent& key_event) {
    return false;
}

void BookmarkEditorView::GetAccessibleState(ui::AXViewState* state) {
  views::DialogDelegateView::GetAccessibleState(state);
  state->name =
      l10n_util::GetStringUTF16(IDS_BOOKMARK_EDITOR_TITLE);
}

void BookmarkEditorView::ButtonPressed(views::Button* sender,
                                       const ui::Event& event) {
  DCHECK_EQ(new_folder_button_.get(), sender);
  NewFolder();
}

bool BookmarkEditorView::IsCommandIdChecked(int command_id) const {
  return false;
}

bool BookmarkEditorView::IsCommandIdEnabled(int command_id) const {
  switch (command_id) {
    case IDS_EDIT:
    case IDS_DELETE:
      return !running_menu_for_root_;
    case IDS_BOOKMARK_EDITOR_NEW_FOLDER_MENU_ITEM:
      return true;
    default:
      NOTREACHED();
      return false;
  }
}

bool BookmarkEditorView::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* accelerator) {
  return GetWidget()->GetAccelerator(command_id, accelerator);
}

void BookmarkEditorView::ExecuteCommand(int command_id, int event_flags) {
  DCHECK(tree_view_->GetSelectedNode());
  if (command_id == IDS_EDIT) {
    tree_view_->StartEditing(tree_view_->GetSelectedNode());
  } else if (command_id == IDS_DELETE) {
    EditorNode* node = tree_model_->AsNode(tree_view_->GetSelectedNode());
    if (!node)
      return;
    if (node->value != 0) {
      const BookmarkNode* b_node =
          bookmarks::GetBookmarkNodeByID(bb_model_, node->value);
      if (!b_node->empty() &&
          !chrome::ConfirmDeleteBookmarkNode(b_node,
            GetWidget()->GetNativeWindow())) {
        // The folder is not empty and the user didn't confirm.
        return;
      }
      deletes_.push_back(node->value);
    }
    tree_model_->Remove(node->parent(), node);
  } else {
    DCHECK_EQ(IDS_BOOKMARK_EDITOR_NEW_FOLDER_MENU_ITEM, command_id);
    NewFolder();
  }
}

void BookmarkEditorView::Show(gfx::NativeWindow parent) {
  constrained_window::CreateBrowserModalDialogViews(this, parent);
  UserInputChanged();
  if (show_tree_ && bb_model_->loaded())
    ExpandAndSelect();
  GetWidget()->Show();
  // Select all the text in the name Textfield.
  title_tf_->SelectAll(true);
  // Give focus to the name Textfield.
  title_tf_->RequestFocus();
}

void BookmarkEditorView::ShowContextMenuForView(
    views::View* source,
    const gfx::Point& point,
    ui::MenuSourceType source_type) {
  DCHECK_EQ(tree_view_, source);
  if (!tree_view_->GetSelectedNode())
    return;
  running_menu_for_root_ =
      (tree_model_->GetParent(tree_view_->GetSelectedNode()) ==
       tree_model_->GetRoot());

  context_menu_runner_.reset(new views::MenuRunner(
      GetMenuModel(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU));

  if (context_menu_runner_->RunMenuAt(source->GetWidget()->GetTopLevelWidget(),
                                      NULL,
                                      gfx::Rect(point, gfx::Size()),
                                      views::MENU_ANCHOR_TOPRIGHT,
                                      source_type) ==
      views::MenuRunner::MENU_DELETED) {
    return;
  }
}

const char* BookmarkEditorView::GetClassName() const {
  return "BookmarkEditorView";
}

void BookmarkEditorView::BookmarkNodeMoved(BookmarkModel* model,
                                           const BookmarkNode* old_parent,
                                           int old_index,
                                           const BookmarkNode* new_parent,
                                           int new_index) {
  Reset();
}

void BookmarkEditorView::BookmarkNodeAdded(BookmarkModel* model,
                                           const BookmarkNode* parent,
                                           int index) {
  Reset();
}

void BookmarkEditorView::BookmarkNodeRemoved(
    BookmarkModel* model,
    const BookmarkNode* parent,
    int index,
    const BookmarkNode* node,
    const std::set<GURL>& removed_urls) {
  if ((details_.type == EditDetails::EXISTING_NODE &&
       details_.existing_node->HasAncestor(node)) ||
      (parent_ && parent_->HasAncestor(node))) {
    // The node, or its parent was removed. Close the dialog.
    GetWidget()->Close();
  } else {
    Reset();
  }
}

void BookmarkEditorView::BookmarkAllUserNodesRemoved(
    BookmarkModel* model,
    const std::set<GURL>& removed_urls) {
  Reset();
}

void BookmarkEditorView::BookmarkNodeChildrenReordered(
    BookmarkModel* model,
    const BookmarkNode* node) {
  Reset();
}

void BookmarkEditorView::Init() {
  bb_model_->AddObserver(this);

  title_label_ = new views::Label(
      l10n_util::GetStringUTF16(IDS_BOOKMARK_EDITOR_NAME_LABEL));

  base::string16 title;
  GURL url;
  if (details_.type == EditDetails::EXISTING_NODE) {
    title = details_.existing_node->GetTitle();
    url = details_.existing_node->url();
  } else if (details_.type == EditDetails::NEW_FOLDER) {
    title = l10n_util::GetStringUTF16(IDS_BOOKMARK_EDITOR_NEW_FOLDER_NAME);
  } else if (details_.type == EditDetails::NEW_URL) {
    url = details_.url;
    title = details_.title;
  }
  title_tf_ = new views::Textfield;
  title_tf_->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_BOOKMARK_AX_EDITOR_NAME_LABEL));
  title_tf_->SetText(title);
  title_tf_->set_controller(this);

  if (show_tree_) {
    tree_view_ = new views::TreeView;
    tree_view_->SetRootShown(false);
    tree_view_->set_context_menu_controller(this);

    new_folder_button_.reset(new views::LabelButton(this,
        l10n_util::GetStringUTF16(IDS_BOOKMARK_EDITOR_NEW_FOLDER_BUTTON)));
    new_folder_button_->SetStyle(views::Button::STYLE_BUTTON);
    new_folder_button_->set_owned_by_client();
    new_folder_button_->SetEnabled(false);
  }

  GridLayout* layout = GridLayout::CreatePanel(this);
  SetLayoutManager(layout);

  const int labels_column_set_id = 0;
  const int single_column_view_set_id = 1;
  const int buttons_column_set_id = 2;

  views::ColumnSet* column_set = layout->AddColumnSet(labels_column_set_id);
  column_set->AddColumn(views::kControlLabelGridAlignment, GridLayout::CENTER,
                        0, GridLayout::USE_PREF, 0, 0);
  column_set->AddPaddingColumn(0, views::kRelatedControlHorizontalSpacing);
  column_set->AddColumn(GridLayout::FILL, GridLayout::CENTER, 1,
                        GridLayout::USE_PREF, 0, 0);

  column_set = layout->AddColumnSet(single_column_view_set_id);
  column_set->AddColumn(GridLayout::FILL, GridLayout::FILL, 1,
                        GridLayout::USE_PREF, 0, 0);

  column_set = layout->AddColumnSet(buttons_column_set_id);
  column_set->AddColumn(GridLayout::FILL, GridLayout::LEADING, 0,
                        GridLayout::USE_PREF, 0, 0);
  column_set->AddPaddingColumn(1, views::kRelatedControlHorizontalSpacing);
  column_set->AddColumn(GridLayout::FILL, GridLayout::LEADING, 0,
                        GridLayout::USE_PREF, 0, 0);
  column_set->AddPaddingColumn(0, views::kRelatedControlHorizontalSpacing);
  column_set->AddColumn(GridLayout::FILL, GridLayout::LEADING, 0,
                        GridLayout::USE_PREF, 0, 0);
  column_set->LinkColumnSizes(0, 2, 4, -1);

  layout->StartRow(0, labels_column_set_id);
  layout->AddView(title_label_);
  layout->AddView(title_tf_);

  if (details_.GetNodeType() != BookmarkNode::FOLDER) {
    url_label_ = new views::Label(
        l10n_util::GetStringUTF16(IDS_BOOKMARK_EDITOR_URL_LABEL));

    url_tf_ = new views::Textfield;
    url_tf_->SetText(chrome::FormatBookmarkURLForDisplay(url));
    url_tf_->set_controller(this);
    url_tf_->SetAccessibleName(
        l10n_util::GetStringUTF16(IDS_BOOKMARK_AX_EDITOR_URL_LABEL));

    layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);

    layout->StartRow(0, labels_column_set_id);
    layout->AddView(url_label_);
    layout->AddView(url_tf_);
  }

  if (show_tree_) {
    layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);
    layout->StartRow(1, single_column_view_set_id);
    layout->AddView(tree_view_->CreateParentIfNecessary());
  }

  layout->AddPaddingRow(0, views::kRelatedControlVerticalSpacing);

  if (!show_tree_ || bb_model_->loaded())
    Reset();
}

void BookmarkEditorView::Reset() {
  if (!show_tree_) {
    if (parent())
      UserInputChanged();
    return;
  }

  new_folder_button_->SetEnabled(true);

  // Do this first, otherwise when we invoke SetModel with the real one
  // tree_view will try to invoke something on the model we just deleted.
  tree_view_->SetModel(NULL);

  EditorNode* root_node = CreateRootNode();
  tree_model_.reset(new EditorTreeModel(root_node));

  tree_view_->SetModel(tree_model_.get());
  tree_view_->SetController(this);

  context_menu_runner_.reset();

  if (parent())
    ExpandAndSelect();
}

GURL BookmarkEditorView::GetInputURL() const {
  if (details_.GetNodeType() == BookmarkNode::FOLDER)
    return GURL();
  return url_formatter::FixupURL(base::UTF16ToUTF8(url_tf_->text()),
                                 std::string());
}

void BookmarkEditorView::UserInputChanged() {
  if (details_.GetNodeType() != BookmarkNode::FOLDER) {
    const GURL url(GetInputURL());
    if (!url.is_valid())
      url_tf_->SetBackgroundColor(kErrorColor);
    else
      url_tf_->UseDefaultBackgroundColor();
  }
  GetDialogClientView()->UpdateDialogButtons();
}

void BookmarkEditorView::NewFolder() {
  // Create a new entry parented to the selected item, or the bookmark
  // bar if nothing is selected.
  EditorNode* parent = tree_model_->AsNode(tree_view_->GetSelectedNode());
  if (!parent) {
    NOTREACHED();
    return;
  }

  tree_view_->StartEditing(AddNewFolder(parent));
}

BookmarkEditorView::EditorNode* BookmarkEditorView::AddNewFolder(
    EditorNode* parent) {
  EditorNode* new_node = new EditorNode(
      l10n_util::GetStringUTF16(IDS_BOOKMARK_EDITOR_NEW_FOLDER_NAME), 0);
  // |new_node| is now owned by |parent|.
  tree_model_->Add(parent, new_node, parent->child_count());
  return new_node;
}

void BookmarkEditorView::ExpandAndSelect() {
  BookmarkExpandedStateTracker::Nodes expanded_nodes =
      bb_model_->expanded_state_tracker()->GetExpandedNodes();
  for (BookmarkExpandedStateTracker::Nodes::const_iterator i(
       expanded_nodes.begin()); i != expanded_nodes.end(); ++i) {
    EditorNode* editor_node =
        FindNodeWithID(tree_model_->GetRoot(), (*i)->id());
    if (editor_node)
      tree_view_->Expand(editor_node);
  }

  const BookmarkNode* to_select = parent_;
  if (details_.type == EditDetails::EXISTING_NODE)
    to_select = details_.existing_node->parent();
  int64_t folder_id_to_select = to_select->id();
  EditorNode* b_node =
      FindNodeWithID(tree_model_->GetRoot(), folder_id_to_select);
  if (!b_node)
    b_node = tree_model_->GetRoot()->GetChild(0);  // Bookmark bar node.

  tree_view_->SetSelectedNode(b_node);
}

BookmarkEditorView::EditorNode* BookmarkEditorView::CreateRootNode() {
  EditorNode* root_node = new EditorNode(base::string16(), 0);
  const BookmarkNode* bb_root_node = bb_model_->root_node();
  CreateNodes(bb_root_node, root_node);
  DCHECK(root_node->child_count() >= 2 && root_node->child_count() <= 4);
  DCHECK_EQ(BookmarkNode::BOOKMARK_BAR, bb_root_node->GetChild(0)->type());
  DCHECK_EQ(BookmarkNode::OTHER_NODE, bb_root_node->GetChild(1)->type());
  if (root_node->child_count() >= 3)
    DCHECK_EQ(BookmarkNode::MOBILE, bb_root_node->GetChild(2)->type());
  return root_node;
}

void BookmarkEditorView::CreateNodes(const BookmarkNode* bb_node,
                                     BookmarkEditorView::EditorNode* b_node) {
  for (int i = 0; i < bb_node->child_count(); ++i) {
    const BookmarkNode* child_bb_node = bb_node->GetChild(i);
    if (child_bb_node->IsVisible() && child_bb_node->is_folder() &&
        bb_model_->client()->CanBeEditedByUser(child_bb_node)) {
      EditorNode* new_b_node = new EditorNode(child_bb_node->GetTitle(),
                                              child_bb_node->id());
      b_node->Add(new_b_node, b_node->child_count());
      CreateNodes(child_bb_node, new_b_node);
    }
  }
}

BookmarkEditorView::EditorNode* BookmarkEditorView::FindNodeWithID(
    BookmarkEditorView::EditorNode* node,
    int64_t id) {
  if (node->value == id)
    return node;
  for (int i = 0; i < node->child_count(); ++i) {
    EditorNode* result = FindNodeWithID(node->GetChild(i), id);
    if (result)
      return result;
  }
  return NULL;
}

void BookmarkEditorView::ApplyEdits() {
  DCHECK(bb_model_->loaded());

  if (tree_view_)
    tree_view_->CommitEdit();

  EditorNode* parent = show_tree_ ?
      tree_model_->AsNode(tree_view_->GetSelectedNode()) : NULL;
  if (show_tree_ && !parent) {
    NOTREACHED();
    return;
  }
  ApplyEdits(parent);
}

void BookmarkEditorView::ApplyEdits(EditorNode* parent) {
  DCHECK(!show_tree_ || parent);

  // We're going to apply edits to the bookmark bar model, which will call us
  // back. Normally when a structural edit occurs we reset the tree model.
  // We don't want to do that here, so we remove ourselves as an observer.
  bb_model_->RemoveObserver(this);

  GURL new_url(GetInputURL());
  base::string16 new_title(title_tf_->text());

  if (!show_tree_) {
    BookmarkEditor::ApplyEditsWithNoFolderChange(
        bb_model_, parent_, details_, new_title, new_url);
    return;
  }

  // Create the new folders and update the titles.
  const BookmarkNode* new_parent = NULL;
  ApplyNameChangesAndCreateNewFolders(
      bb_model_->root_node(), tree_model_->GetRoot(), parent, &new_parent);

  BookmarkEditor::ApplyEditsWithPossibleFolderChange(
      bb_model_, new_parent, details_, new_title, new_url);

  BookmarkExpandedStateTracker::Nodes expanded_nodes;
  UpdateExpandedNodes(tree_model_->GetRoot(), &expanded_nodes);
  bb_model_->expanded_state_tracker()->SetExpandedNodes(expanded_nodes);

  // Remove the folders that were removed. This has to be done after all the
  // other changes have been committed.
  bookmarks::DeleteBookmarkFolders(bb_model_, deletes_);
}

void BookmarkEditorView::ApplyNameChangesAndCreateNewFolders(
    const BookmarkNode* bb_node,
    BookmarkEditorView::EditorNode* b_node,
    BookmarkEditorView::EditorNode* parent_b_node,
    const BookmarkNode** parent_bb_node) {
  if (parent_b_node == b_node)
    *parent_bb_node = bb_node;
  for (int i = 0; i < b_node->child_count(); ++i) {
    EditorNode* child_b_node = b_node->GetChild(i);
    const BookmarkNode* child_bb_node = NULL;
    if (child_b_node->value == 0) {
      // New folder.
      child_bb_node = bb_model_->AddFolder(bb_node,
          bb_node->child_count(), child_b_node->GetTitle());
      child_b_node->value = child_bb_node->id();
    } else {
      // Existing node, reset the title (BookmarkModel ignores changes if the
      // title is the same).
      for (int j = 0; j < bb_node->child_count(); ++j) {
        const BookmarkNode* node = bb_node->GetChild(j);
        if (node->is_folder() && node->id() == child_b_node->value) {
          child_bb_node = node;
          break;
        }
      }
      DCHECK(child_bb_node);
      bb_model_->SetTitle(child_bb_node, child_b_node->GetTitle());
    }
    ApplyNameChangesAndCreateNewFolders(child_bb_node, child_b_node,
                                        parent_b_node, parent_bb_node);
  }
}

void BookmarkEditorView::UpdateExpandedNodes(
    EditorNode* editor_node,
    BookmarkExpandedStateTracker::Nodes* expanded_nodes) {
  if (!tree_view_->IsExpanded(editor_node))
    return;

  // The root is 0.
  if (editor_node->value != 0) {
    expanded_nodes->insert(
        bookmarks::GetBookmarkNodeByID(bb_model_, editor_node->value));
  }

  for (int i = 0; i < editor_node->child_count(); ++i)
    UpdateExpandedNodes(editor_node->GetChild(i), expanded_nodes);
}

ui::SimpleMenuModel* BookmarkEditorView::GetMenuModel() {
  if (!context_menu_model_.get()) {
    context_menu_model_.reset(new ui::SimpleMenuModel(this));
    context_menu_model_->AddItemWithStringId(IDS_EDIT, IDS_EDIT);
    context_menu_model_->AddItemWithStringId(IDS_DELETE, IDS_DELETE);
    context_menu_model_->AddItemWithStringId(
        IDS_BOOKMARK_EDITOR_NEW_FOLDER_MENU_ITEM,
        IDS_BOOKMARK_EDITOR_NEW_FOLDER_MENU_ITEM);
  }
  return context_menu_model_.get();
}

void BookmarkEditorView::EditorTreeModel::SetTitle(
    ui::TreeModelNode* node,
    const base::string16& title) {
  if (!title.empty())
    ui::TreeNodeModel<EditorNode>::SetTitle(node, title);
}

namespace chrome {

void ShowBookmarkEditorViews(gfx::NativeWindow parent_window,
                             Profile* profile,
                             const BookmarkEditor::EditDetails& details,
                             BookmarkEditor::Configuration configuration) {
  DCHECK(profile);
  BookmarkEditorView* editor = new BookmarkEditorView(
      profile, details.parent_node, details, configuration);
  editor->Show(parent_window);
}

}  // namespace chrome
