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
  classID: Components.ID("{0bb628c0-8f22-4273-b966-bae528f3a1d6}"),
  scheme: "pebble",
  protocolFlags: Ci.nsIProtocolHandler.URI_NORELATIVE |
                 Ci.nsIProtocolHandler.URI_NOAUTH |
                 Ci.nsIProtocolHandler.URI_LOADABLE_BY_ANYONE |
                 Ci.nsIProtocolHandler.URI_DOES_NOT_RETURN_DATA,

  newURI(spec, charset, baseURI) {
      dump("newURI: "+spec+"\n");
      const cls = Cc["@mozilla.org/network/standard-url-mutator;1"];
      const newUrl = cls.createInstance(Ci.nsIStandardURLMutator);
      newUrl.init(Ci.nsIStandardURL.URLTYPE_AUTHORITY, 80, spec, charset, baseURI);
      return newUrl.finalize().QueryInterface(Ci.nsIURI);
  },

  newChannel(aURI) {
    dump("URI: "+aURI.spec+"\n");
    let hIdx = aURI.spec.indexOf("://")+3;
    let qIdx = aURI.spec.indexOf("#");
    let action = aURI.spec.substring(hIdx,qIdx-1);
    let query = aURI.spec.substring(qIdx+1);

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

// Nasty JS OOP
function PebbleJsProtoHandler() {
  dump("Adding protocol handler for pebblejs:// URI scheme\n");
  PebbleProtoHandler.call(this)
}
PebbleJsProtoHandler.prototype = Object.create(PebbleProtoHandler.prototype);
PebbleJsProtoHandler.prototype.constructor = PebbleJsProtoHandler;
PebbleJsProtoHandler.prototype.classID = Components.ID("{64D704B0-F5FF-11E5-8DDD-D05ABB8E7F8B}");
PebbleJsProtoHandler.prototype.scheme = "pebblejs";

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([PebbleProtoHandler,PebbleJsProtoHandler]);
