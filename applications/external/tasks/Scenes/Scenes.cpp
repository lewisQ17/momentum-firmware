#include "Scenes.hpp"

void FTasks::Data::writeContainerString(const FTasks::NoteContainer& container, const UFZ::File& file) noexcept
{
    for (size_t i = 0; i < container.size(); i++)
    {
        const auto& a = container[i];
        file.write((void*)a.first.data(), a.first.size());
        file.write((void*)newLine, 1);
        file.write((void*)a.second.data(), a.second.size());

        if (i < (container.size() - 1))
            file.write((void*)newLine, 1);
    }
}

void FTasks::Data::readContainerString(FTasks::NoteContainer& container, const UFZ::File& file) noexcept
{
    std::string content;
    // read exactly file.size() bytes; the extra byte used to add a trailing NUL on every launch
    content.resize(file.size());
    file.read(content.data(), content.size());

    // files saved by older versions contain NUL padding, so strip it from every field
    const auto trimNul = [](std::string str) {
        while (!str.empty() && str.back() == '\0')
            str.pop_back();
        return str;
    };

    container.clear();

    size_t pos;
    size_t previous = 0;
    size_t i = 1;
    for (; (pos = content.find(newLine, previous)) != std::string::npos; i++)
    {
        auto str = trimNul(content.substr(previous, pos - previous));
        previous = pos + 1;
        if ((i % 2) == 0)
            container.back().second = str;
        else
            container.emplace_back(str, "");
    }
    if ((i % 2) == 0)
        container.back().second = trimNul(content.substr(previous, pos));
    else
        container.emplace_back(trimNul(content.substr(previous, pos)), "");
}

void FTasks::Data::save(const UFZ::Application& application) noexcept
{
    UFZ::File file{};

    const auto* ctx = CTX(application.getUserPointer());
    if (file.open(application.getFilesystem(), TASKS_TODO_DATA_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS))
        writeContainerString(ctx->containers.todo, file);
    file.close();
    if (file.open(application.getFilesystem(), TASKS_DONE_DATA_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS))
        writeContainerString(ctx->containers.done, file);
}

void FTasks::Data::load(const UFZ::Application& application) noexcept
{
    UFZ::File file{};

    auto* ctx = CTX(application.getUserPointer());
    if (file.open(application.getFilesystem(), TASKS_TODO_DATA_FILE, FSAM_READ_WRITE, FSOM_OPEN_EXISTING))
    {
        // an empty todo file must not skip loading the done file
        if (file.size() != 0)
            readContainerString(ctx->containers.todo, file);
    }
    file.close();
    if (file.open(application.getFilesystem(), TASKS_DONE_DATA_FILE, FSAM_READ_WRITE, FSOM_OPEN_EXISTING))
    {
        if (file.size() != 0)
            readContainerString(ctx->containers.done, file);
    }
}
