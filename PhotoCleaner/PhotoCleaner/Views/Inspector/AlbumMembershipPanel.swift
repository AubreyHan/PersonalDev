import SwiftUI

/// 右侧检视栏上方的“相簿归属双向管理与新建面板”（FR-6.1 ~ FR-6.3, SPEC §2.1, EC-7）
public struct AlbumMembershipPanel: View {
    @Bindable public var viewModel: AlbumMembershipViewModel
    @State private var isNewAlbumPopoverPresented: Bool = false

    public init(viewModel: AlbumMembershipViewModel) {
        self.viewModel = viewModel
    }

    public init(albumMembershipVM: AlbumMembershipViewModel) {
        self.viewModel = albumMembershipVM
    }

    public init(appViewModel: AppViewModel) {
        self.viewModel = appViewModel.albumMembershipVM
    }

    public var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            headerSection
            searchBarSection
            createAlbumInlineSection
            Divider()
            membershipSectionsList
        }
    }

    // MARK: - Header

    private var headerSection: some View {
        HStack(spacing: 6) {
            Image(systemName: "rectangle.stack.badge.person.crop")
                .foregroundStyle(Color.accentColor)
            Text("相簿归属")
                .font(.subheadline.weight(.semibold))
            if viewModel.selectedCount > 0 {
                Text("已选 \(viewModel.selectedCount) 项")
                    .font(.caption2.monospacedDigit())
                    .padding(.horizontal, 6)
                    .padding(.vertical, 2)
                    .foregroundStyle(.secondary)
                    .background(Color.secondary.opacity(0.14), in: Capsule())
            }
            Spacer()
            if viewModel.isMutating {
                ProgressView()
                    .controlSize(.mini)
            }
        }
    }

    // MARK: - Real-Time Search Bar

    private var searchBarSection: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 6) {
                Image(systemName: "magnifyingglass")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                TextField("搜索相簿或文件夹路径...", text: $viewModel.searchQuery)
                    .textFieldStyle(.plain)
                    .font(.caption)
                    .onSubmit {
                        if viewModel.canQuickCreateFromSearchQuery {
                            let titleToCreate = viewModel.searchQuery
                            Task { @MainActor in
                                _ = await viewModel.createAlbumAndJoin(title: titleToCreate)
                            }
                        }
                    }
                if !viewModel.searchQuery.isEmpty {
                    Button {
                        viewModel.searchQuery = ""
                    } label: {
                        Image(systemName: "xmark.circle.fill")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    .buttonStyle(.plain)
                }
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 6)
            .background(Color(nsColor: .textBackgroundColor), in: RoundedRectangle(cornerRadius: 7))
            .overlay(
                RoundedRectangle(cornerRadius: 7)
                    .strokeBorder(Color.secondary.opacity(0.22), lineWidth: 1)
            )

            if viewModel.canQuickCreateFromSearchQuery {
                Button {
                    let queryTitle = viewModel.searchQuery
                    Task { @MainActor in
                        _ = await viewModel.createAlbumAndJoin(title: queryTitle)
                    }
                } label: {
                    HStack(spacing: 5) {
                        Image(systemName: "plus.circle.fill")
                            .foregroundStyle(Color.accentColor)
                        Text("按回车直接新建「\(viewModel.searchQuery.trimmingCharacters(in: .whitespacesAndNewlines))」并归入")
                            .lineLimit(1)
                            .truncationMode(.middle)
                    }
                    .font(.caption2)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 4)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(Color.accentColor.opacity(0.1), in: RoundedRectangle(cornerRadius: 6))
                }
                .buttonStyle(.plain)
            }
        }
    }

    // MARK: - Inline Create Album & Join (+ 新建相簿并加入)

    private var createAlbumInlineSection: some View {
        DisclosureGroup(isExpanded: $isNewAlbumPopoverPresented) {
            VStack(alignment: .leading, spacing: 8) {
                TextField("输入新相簿名称...", text: $viewModel.newAlbumTitle)
                    .textFieldStyle(.roundedBorder)
                    .font(.caption)
                    .onSubmit {
                        submitNewAlbum()
                    }

                HStack(spacing: 6) {
                    Text("位置:")
                        .font(.caption2)
                        .foregroundStyle(.secondary)

                    Picker("", selection: $viewModel.selectedParentFolderID) {
                        Text("顶层 (我的相簿)").tag(String?.none)
                        ForEach(viewModel.availableFolders) { folder in
                            Text(folder.breadcrumbPath).tag(Optional(folder.id))
                        }
                    }
                    .labelsHidden()
                    .controlSize(.small)
                    .frame(maxWidth: .infinity)

                    Button("创建并加入") {
                        submitNewAlbum()
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.small)
                    .disabled(
                        viewModel.newAlbumTitle.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty ||
                        !viewModel.hasSelection ||
                        viewModel.isMutating
                    )
                }
            }
            .padding(.top, 6)
        } label: {
            HStack(spacing: 5) {
                Image(systemName: "plus.rectangle.on.folder")
                    .font(.caption)
                    .foregroundStyle(Color.accentColor)
                Text("+ 新建相簿并加入")
                    .font(.caption.weight(.medium))
                    .foregroundStyle(Color.accentColor)
                Spacer()
            }
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 6)
        .background(Color(nsColor: .controlBackgroundColor).opacity(0.6), in: RoundedRectangle(cornerRadius: 8))
    }

    // MARK: - Belonging & Not-Joined Sections

    private var membershipSectionsList: some View {
        ScrollView {
            LazyVStack(alignment: .leading, spacing: 14) {
                // 1. 【所属相簿 (M)】
                VStack(alignment: .leading, spacing: 6) {
                    sectionHeader(
                        title: "所属相簿 (\(viewModel.belongingCount))",
                        icon: "checkmark.rectangle.stack.fill",
                        tint: .accentColor
                    )

                    if viewModel.belongingRows.isEmpty {
                        emptyHintRow(
                            viewModel.searchQuery.isEmpty
                                ? "当前选中项暂未归入任何普通相簿"
                                : "无匹配的所属相簿"
                        )
                    } else {
                        ForEach(viewModel.belongingRows) { row in
                            albumRowButton(row)
                        }
                    }
                }

                // 2. 【未加入相簿 (K)】
                VStack(alignment: .leading, spacing: 6) {
                    sectionHeader(
                        title: "未加入相簿 (\(viewModel.notJoinedCount))",
                        icon: "folder",
                        tint: .secondary
                    )

                    if viewModel.notJoinedRows.isEmpty {
                        emptyHintRow(
                            viewModel.allAlbums.isEmpty
                                ? "图库中暂无可编辑的普通相簿"
                                : "无更多未加入相簿"
                        )
                    } else {
                        ForEach(viewModel.notJoinedRows) { row in
                            albumRowButton(row)
                        }
                    }
                }
            }
            .padding(.vertical, 2)
        }
    }

    private func sectionHeader(title: String, icon: String, tint: Color) -> some View {
        HStack(spacing: 5) {
            Image(systemName: icon)
                .font(.caption2)
                .foregroundStyle(tint)
            Text(title)
                .font(.caption.weight(.semibold))
                .foregroundStyle(.secondary)
            Spacer()
        }
        .padding(.horizontal, 4)
    }

    private func emptyHintRow(_ text: String) -> some View {
        Text(text)
            .font(.caption)
            .foregroundStyle(.tertiary)
            .padding(.horizontal, 8)
            .padding(.vertical, 6)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Color(nsColor: .controlBackgroundColor).opacity(0.35), in: RoundedRectangle(cornerRadius: 6))
    }

    private func albumRowButton(_ row: AlbumMembershipRow) -> some View {
        Button {
            Task { @MainActor in
                await viewModel.toggleMembership(for: row)
            }
        } label: {
            HStack(spacing: 8) {
                stateBadge(for: row.relation)

                VStack(alignment: .leading, spacing: 2) {
                    Text(row.album.breadcrumbPath)
                        .font(.caption.weight(row.relation.isBelonging ? .medium : .regular))
                        .foregroundStyle(.primary)
                        .lineLimit(1)
                        .truncationMode(.middle)

                    if row.relation == .partiallySelectedInAlbum {
                        Text("部分归属 (\(row.matchedSelectedCount)/\(row.totalSelectedCount)) · 点击全部补齐加入")
                            .font(.system(size: 10))
                            .foregroundStyle(.orange)
                    }
                }

                Spacer(minLength: 4)

                if row.relation == .partiallySelectedInAlbum {
                    Text("\(row.matchedSelectedCount)/\(row.totalSelectedCount)")
                        .font(.caption2.monospacedDigit().weight(.semibold))
                        .padding(.horizontal, 5)
                        .padding(.vertical, 1)
                        .foregroundStyle(.orange)
                        .background(Color.orange.opacity(0.15), in: Capsule())
                }

                Text("\(row.album.assetCount)")
                    .font(.caption2.monospacedDigit())
                    .foregroundStyle(.secondary)
                    .padding(.horizontal, 6)
                    .padding(.vertical, 2)
                    .background(Color.secondary.opacity(0.12), in: Capsule())
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 6)
            .contentShape(Rectangle())
            .background(
                RoundedRectangle(cornerRadius: 7)
                    .fill(rowBackgroundColor(for: row.relation))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 7)
                    .strokeBorder(rowBorderColor(for: row.relation), lineWidth: 1)
            )
        }
        .buttonStyle(.plain)
        .disabled(!viewModel.hasSelection || viewModel.isMutating)
        .help(tooltipText(for: row))
    }

    @ViewBuilder
    private func stateBadge(for relation: AlbumMembershipRelation) -> some View {
        switch relation {
        case .allSelectedInAlbum:
            Image(systemName: "checkmark.square.fill")
                .font(.system(size: 14, weight: .semibold))
                .foregroundStyle(Color.accentColor)
        case .partiallySelectedInAlbum:
            Image(systemName: "minus.square.fill")
                .font(.system(size: 14, weight: .semibold))
                .foregroundStyle(.orange)
        case .noneSelectedInAlbum:
            Image(systemName: "square")
                .font(.system(size: 14))
                .foregroundStyle(.secondary)
        }
    }

    private func rowBackgroundColor(for relation: AlbumMembershipRelation) -> Color {
        switch relation {
        case .allSelectedInAlbum:
            return Color.accentColor.opacity(0.12)
        case .partiallySelectedInAlbum:
            return Color.orange.opacity(0.10)
        case .noneSelectedInAlbum:
            return Color(nsColor: .controlBackgroundColor).opacity(0.5)
        }
    }

    private func rowBorderColor(for relation: AlbumMembershipRelation) -> Color {
        switch relation {
        case .allSelectedInAlbum:
            return Color.accentColor.opacity(0.35)
        case .partiallySelectedInAlbum:
            return Color.orange.opacity(0.35)
        case .noneSelectedInAlbum:
            return Color.clear
        }
    }

    private func tooltipText(for row: AlbumMembershipRow) -> String {
        switch row.relation {
        case .allSelectedInAlbum:
            return "点击将选中项从「\(row.album.breadcrumbPath)」中移出"
        case .partiallySelectedInAlbum:
            return "当前 \(row.matchedSelectedCount)/\(row.totalSelectedCount) 项已在该相簿中，点击将全部选中项补齐加入「\(row.album.breadcrumbPath)」"
        case .noneSelectedInAlbum:
            return "点击将选中项加入「\(row.album.breadcrumbPath)」"
        }
    }

    private func submitNewAlbum() {
        let trimmed = viewModel.newAlbumTitle.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, viewModel.hasSelection else { return }
        Task { @MainActor in
            if await viewModel.createAlbumAndJoin() != nil {
                isNewAlbumPopoverPresented = false
            }
        }
    }
}
