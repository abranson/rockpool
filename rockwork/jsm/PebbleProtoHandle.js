/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * PebbleProtoHandle.js
 */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu, results: Cr} = Components;

Cu.import('resource://gre/modules/XPCOMUtils.jsm');
Cu.import("resource://gre/modules/Services.jsm");

XPCOMUtils.defineLazyServiceGetter(Services, "embedlite",
  "@mozilla.org/embedlite-app-service;1", "nsIEmbedAppService");

function PebbleProtoHandler() {
  dump("Adding protocol handler for pebble:// URI scheme\n")
}

PebbleProtoHandler.prototype = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIProtocolHandler]),
  classID: Components.ID("{64D704B0-F5FF-11E5-8DDD-D05ABB8E7F8B}"),
  scheme: "pebblejs",
  defaultPort: -1,
  protocolFlags: Ci.nsIProtocolHandler.URI_NORELATIVE |
                 Ci.nsIProtocolHandler.URI_NOAUTH |
                 Ci.nsIProtocolHandler.URI_LOADABLE_BY_ANYONE |
                 Ci.nsIProtocolHandler.URI_DOES_NOT_RETURN_DATA,
  allowPort: function() false,

  newURI: function Proto_newURI(aSpec, aOriginCharset) {
    let uri = Cc["@mozilla.org/network/simple-uri;1"].createInstance(Ci.nsIURI);
    uri.spec = aSpec;
    return uri;
  },

  newChannel: function Proto_newChannel(aURI) {
    let action = aURI.spec.substring(11,16);
    let query = aURI.spec.split("#")[1];

    let win = Services.embedlite.getAnyEmbedWindow(true);
    if(win) {
      let winid = Services.embedlite.getIDByWindow(win);
      //dump("About to send message "+action+" to window "+winid+"\n");
      Services.embedlite.sendAsyncMessage(winid, "embed:pebble", JSON.stringify({
        "action":action,
        "query": query,
        "uri": aURI.spec
      }));
      //dump("Pebble sent the query "+query+"\n");
      return Services.io.newChannel("file:///dev/null",null,null);
    }
    throw Components.results.NS_ERROR_ILLEGAL_VALUE;
  }
};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([PebbleProtoHandler]);
