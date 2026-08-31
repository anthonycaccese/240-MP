import QtQuick
import Components

FocusScope {
    id: editRoot

    focus: true
    property var navParams: ({})
    readonly property string moduleIcon:
        appCore ? (appCore.get_module_info(moduleId).icon || "") : ""
    property int    channelNumber: navParams.channelNumber !== undefined ? navParams.channelNumber : -1
    property string channelName:   navParams.channelName   || ""
    property int    bookingIndex:  navParams.bookingIndex !== undefined ? navParams.bookingIndex : -1
    property var    navListState:  navParams.navListState  || ({})

    property string moduleId: navParams.moduleId || ""

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property string bookingName: ""
    property int hour: 20
    property int minute: 0
    property var days: []
    property int films: 0
    property var genres: []
    property var collections: []
    property var playlists: []
    property bool anyFilm: false
    property var match: []
    property int criteria: 0
    property string source: "plex"
    readonly property string genreWord: source === "plex" ? "Categories" : "Genres"
    property string folder: ""
    property string status: ""
    property bool armedToDelete: false

    readonly property var rows: {
        var r = ["name", "hour", "minute", "days"]
        if (source === "local") {
            // Either way of saying which films: pick them one by one from the
            // library, or point the slot at a folder and let it choose.
            r.push("films")
            r.push("folder")
        } else {
            r.push("anyfilm")
            r.push("films")
            r.push("genres")
            r.push("collections")
            if (source === "plex") r.push("playlists")
        }
        r.push("delete")
        return r
    }
    readonly property int rowCount: rows.length

    property int current: 0

    function reload() {
        var all = virtualChannelsBackend.channel_bookings(channelNumber)
        if (bookingIndex < 0 || bookingIndex >= all.length) { goBack(); return }
        var b = all[bookingIndex]
        bookingName = b.name
        hour   = b.hour
        minute = b.minute
        days   = b.days
        films  = b.films
        genres = b.genres
        collections = b.collections
        playlists = b.playlists
        anyFilm = b.anyFilm
        match  = b.match
        criteria = b.criteria
        source = b.source
        folder = b.folder
    }

    function hourText() {
        var suffix = hour < 12 ? "AM" : "PM"
        var hh = hour % 12
        if (hh === 0) hh = 12
        return hh + " " + suffix
    }

    function minuteText() {
        return minute < 10 ? "0" + minute : String(minute)
    }

    function daysText() {
        if (days.length === 0) return "EVERY DAY"
        var order = ["mon", "tue", "wed", "thu", "fri", "sat", "sun"]
        var out = []
        for (var i = 0; i < order.length; i++)
            if (days.indexOf(order[i]) >= 0) out.push(order[i].toUpperCase())
        return out.length > 0 ? out.join(" ") : "NEVER"
    }

    function countText(n, one, many) {
        if (n === 0) return "NONE"
        return n === 1 ? "1 " + one : n + " " + many
    }

    function folderText() {
        return folder === "" ? "NO FOLDER" : folder.split("/").pop().toUpperCase()
    }

    function poolSummary() {
        if (criteria === 0) return anyFilm ? "ANY MOVIE" : "NOTHING YET"
        var bits = []
        if (films > 0)            bits.push(countText(films, "MOVIE", "MOVIES"))
        if (genres.length > 0)    bits.push(countText(genres.length, "GENRE", "GENRES"))
        if (collections.length > 0) bits.push(countText(collections.length, "SET", "SETS"))
        if (playlists.length > 0)   bits.push(countText(playlists.length, "LIST", "LISTS"))
        if (match.length > 0)     bits.push(match.join(" / ").toUpperCase())
        return bits.join(" · ")
    }

    function commitTime() {
        var hhmm = (hour < 10 ? "0" + hour : hour) + ":" + (minute < 10 ? "0" + minute : minute)
        if (!virtualChannelsBackend.set_booking_time(channelNumber, bookingIndex, hhmm))
            status = "Could not save the time"
        else
            status = ""
    }

    function step(delta) {
        if (rows[current] === "anyfilm") { toggleAnyFilm(); return }
        if (rows[current] === "hour") {
            hour = (hour + delta + 24) % 24
            commitTime()
        } else if (rows[current] === "minute") {
            minute = (minute + delta * 5 + 60) % 60
            commitTime()
        }
    }

    function labelFor(i) {
        switch (rows[i]) {
        case "name":        return "Name"
        case "hour":        return "Hour"
        case "minute":      return "Minute"
        case "days":        return "Days"
        case "folder":      return "Folder"
        case "anyfilm":     return "Any Movie"
        case "films":       return "Movies"
        case "genres":      return editRoot.genreWord
        case "collections": return "Collections"
        case "playlists":   return "Playlists"
        case "delete":      return armedToDelete ? "Press Again To Delete" : "Delete This Slot"
        }
        return ""
    }

    function valueFor(i) {
        switch (rows[i]) {
        case "name":        return editRoot.bookingName.toUpperCase()
        case "hour":        return editRoot.hourText()
        case "minute":      return editRoot.minuteText()
        case "days":        return editRoot.daysText()
        case "folder":      return editRoot.folderText()
        case "anyfilm":     return editRoot.anyFilm ? "ON" : "OFF"
        case "films":       return editRoot.countText(editRoot.films, "MOVIE", "MOVIES")
        case "genres":      return editRoot.countText(editRoot.genres.length,
                                                       editRoot.genreWord.toUpperCase().slice(0, -1),
                                                       editRoot.genreWord.toUpperCase())
        case "collections": return editRoot.countText(editRoot.collections.length, "SET", "SETS")
        case "playlists":   return editRoot.countText(editRoot.playlists.length, "LIST", "LISTS")
        }
        return ""
    }

    function helpFor(i) {
        switch (rows[i]) {
        case "name":   return "What to call this slot. It is for you, not for the guide."
        case "hour":   return "Left and right to change the hour."
        case "minute": return "Left and right in five-minute steps."
        case "days":   return "Which days it airs. None chosen means every day."
        case "folder": return editRoot.films > 0
                              ? "A folder of movies, on top of the ones picked above. Trailers and extras in it are skipped."
                              : "A folder this slot draws its movies from. Trailers and extras in it are skipped."
        case "anyfilm": return editRoot.anyFilm
                               ? "The whole movie library. Turn this off to pick what airs."
                               : "Off, so only what you pick below can air. Turn on for any movie."
        case "films":  return editRoot.source === "local"
                              ? ("Draws on: " + editRoot.poolSummary()
                                 + ". Movies picked by name, plus anything in the folder below.")
                              : ("Draws on: " + editRoot.poolSummary()
                                 + ". Pick movies by name; anything ticked anywhere can air.")
        case "genres": return "Draw on a whole " + (editRoot.source === "plex" ? "category" : "genre")
                              + " — every horror movie, say — rather than named titles."
        case "collections": return "Draw on a collection. Smart collections work too — everything in it becomes eligible."
        case "playlists":   return "Draw on a playlist. Smart playlists work too — everything in it becomes eligible."
        case "delete": return "Remove this slot. The channel keeps everything else."
        }
        return ""
    }

    function cycles(i) {
        return rows[i] === "hour" || rows[i] === "minute" || rows[i] === "anyfilm"
    }

    function toggleAnyFilm() {
        if (!virtualChannelsBackend.set_booking_any_film(channelNumber, bookingIndex, !anyFilm))
            status = "Could not save"
        else {
            status = ""
            reload()
        }
    }

    function open(i) {
        var row = rows[i]
        if (row !== "delete") armedToDelete = false

        if (row === "name") {
            appCore.save_setting(moduleId, "booking_name_buffer", "")
            navigateTo("modules/virtual_channels/views/TextEntry.qml", {
                moduleId: editRoot.moduleId,
                settingKey: "booking_name_buffer",
                title: "Name This Slot",
                initialText: editRoot.bookingName
            }, { currentIndex: editRoot.current, namedBooking: true })
            return
        }
        if (row === "days") {
            navigateTo("modules/virtual_channels/views/BookingDays.qml", {
                moduleId:      editRoot.moduleId,
                channelNumber: editRoot.channelNumber,
                bookingIndex:  editRoot.bookingIndex,
                title:         editRoot.bookingName
            }, { currentIndex: editRoot.current })
            return
        }
        if (row === "folder") {
            {
                appCore.save_setting(moduleId, "booking_folder_buffer", "")
                navigateTo("views/DirectoryBrowser.qml", {
                    moduleId: editRoot.moduleId,
                    settingKey: "booking_folder_buffer",
                    currentPath: editRoot.folder !== "" ? editRoot.folder
                                                        : virtualChannelsBackend.media_root()
                }, { currentIndex: editRoot.current, pickedFolder: true })
            }
            return
        }
        if (row === "films" && source === "local") {
            navigateTo("modules/virtual_channels/views/SourceBrowser.qml", {
                moduleId:      editRoot.moduleId,
                channelNumber: editRoot.channelNumber,
                kind:          "movies",
                bookingIndex:  editRoot.bookingIndex,
                title:         editRoot.bookingName
            }, { currentIndex: editRoot.current })
            return
        }
        if (row === "films" || row === "genres" || row === "collections" || row === "playlists") {
            navigateTo("modules/virtual_channels/views/SourceBrowser.qml", {
                moduleId:      editRoot.moduleId,
                channelNumber: editRoot.channelNumber,
                kind: row === "films"   ? "movies"
                    : row === "genres"  ? "moviegenres"
                    : row === "playlists" ? "movieplaylists" : "moviecollections",
                bookingIndex: editRoot.bookingIndex,
                title: editRoot.bookingName
            }, { currentIndex: editRoot.current })
            return
        }
        if (row === "delete") {
            if (!armedToDelete) {
                armedToDelete = true
                status = "Press again to delete this slot, or move away to keep it"
                return
            }
            if (virtualChannelsBackend.delete_booking(channelNumber, bookingIndex))
                goBack()
            else
                status = "Could not delete it"
        }
    }

    Component.onCompleted: {
        reload()
        if (navListState.currentIndex !== undefined) current = navListState.currentIndex

        if (navListState.namedBooking) {
            var typed = appCore.get_setting(moduleId, "booking_name_buffer")
            if (typed && typed !== "" && typed !== bookingName) {
                if (virtualChannelsBackend.set_booking_name(channelNumber, bookingIndex, typed))
                    reload()
                else
                    status = "Could not save the name"
            }
        }
        if (navListState.pickedFolder) {
            var picked = appCore.get_setting(moduleId, "booking_folder_buffer")
            if (picked && picked !== "") {
                if (virtualChannelsBackend.set_booking_folder(channelNumber, bookingIndex, picked))
                    reload()
                else
                    status = "Could not save the folder"
            }
        }
    }

    OptionList {
        anchors.fill: parent
        focus: true
        iconSource: editRoot.moduleIcon
        title: /slot\s*$/i.test(editRoot.bookingName)
               ? editRoot.bookingName
               : editRoot.bookingName + " — Slot"
        rows: editRoot.rows
        current: editRoot.current
        onCurrentChanged: {
            editRoot.current = current
            if (editRoot.armedToDelete) {
                editRoot.armedToDelete = false
                editRoot.status = ""
            }
        }
        status: editRoot.status
        labelFor: function(i) { return editRoot.labelFor(i) }
        valueFor: function(i) { return editRoot.valueFor(i) }
        helpFor:  function(i) { return editRoot.helpFor(i) }
        cycles:   function(i) { return editRoot.cycles(i) }
        onStep:     function(d) { editRoot.step(d) }
        onActivate: function(i) { editRoot.open(i) }
        onBack:     function() { editRoot.goBack() }
    }
}
