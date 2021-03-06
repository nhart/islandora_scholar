<?php

class UploadView {
  public static function get_tabs($pid) {
    $tab_set = array();
    module_load_include('inc', 'fedora_repository', 'api/fedora_item');
    $item = new Fedora_Item($pid);
    if(user_access('ingest new fedora objects' || $is_owner)) {
      $has_pdf = array_key_exists('PDF', $item->datastreams);
      $tab_set += array(
        'upload' => array(
          '#type' => 'tabpage',
          '#tab_name' => 'attach-doc',
          '#title' => ($has_pdf ?
            t('Attached Document'):
            t('Attach Document')),
          '#content' => ($has_pdf ?
            drupal_get_form('islandora_scholar_modify_form', $pid):
            drupal_get_form('islandora_scholar_upload_form', $pid)),
        ),
      );
    }

    return $tab_set;
  }
}

function islandora_scholar_modify_form($form_state, $pid) {
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  $item = new Fedora_Item($pid);
  $mods_str = trim($item->get_datastream_dissemination('MODS'));
  $mods_doc = DOMDocument::loadXML($mods_str);
  $mods_xpath = new DOMXPath($mods_doc);
  $mods_xpath->registerNamespace('m', 'http://www.loc.gov/mods/v3');
  $usage = t('No usage statement');
  $mods_usages = $mods_xpath->query('//m:mods/m:accessCondition[@type="use and reproduction"]');
  if ($mods_usages->length > 0) {
    $usage = $mods_usages->item(0)->textContent;
  }
  $version = t('No version statement');
  $mods_versions = $mods_xpath->query('//m:mods/m:physicalDescription[@authority="local"]');
  if ($mods_versions->length > 0) {
    $version = $mods_versions->item(0)->textContent;
  }

  return array(
    'pid' => array( //Store the PID.
      '#type' => 'value',
      '#value' => $pid,
    ),
    'dsid' => array(
      '#type' => 'value',
      '#value' => 'PDF',
    ),
    'usage' => array(
      '#type' => 'item',
      '#title' => t('Usage'),
      '#value' => $usage,
    ),
    'version' => array(
      '#type' => 'item',
      '#title' => t('Publication version'),
      '#value' => $version,
    ),
    'delete_document' => array(
      '#name' => 'delete_document',
      '#type' => 'submit',
      '#value' => t('Delete Document/PDF'),
    ),
  );
}

function islandora_scholar_modify_form_submit($form, &$form_state) {
  extract($form_state['values']);
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  $item = new Fedora_Item($pid);

  if ($form_state['clicked_button']['#name'] == 'delete_document') {
    //TODO:  Also remove the PDF's derivatives.
    $status = $item->purge_datastream($dsid);

    if ($status === NULL) {
      drupal_set_message(t('DON\'T PANIC!, but we may have failed purging the ' .
        'datastream...  Some debug info: @info', array('@info' => print_r($status, TRUE))));
    }
  }
}

function islandora_scholar_upload_form(&$form_state, $pid) {
  return array(
    '#type' => 'form',
    'upload_document' => array(
      '#type' => 'fieldset',
      '#title' => t('Upload Document'),
      'pid' => array(
        '#type' => 'value',
        '#value' => $pid,
      ),
      'dsid' => array(
        '#type' => 'value',
        '#value' => 'PDF',
      ),
      'file' => array(
        '#type' => 'file', //Might want to make this use an AJAX uploader? (upload_element?)
        '#title' => 'PDF file',
      ),
      'version' => array(
        '#type' => 'radios',
        '#title' => t('Document Version'),
        '#options' => array(
          'PRE-PUBLICATION' => t('Pre-Publication'),
          'PUBLISHED' => t('Published'),
          'POST-PUBLICATION' => t('Post-Publication'),
          'OTHER' => t('Other'),
        ),
        '#required' => TRUE,
      ),
      'usage' => array(
        '#type' => 'radios',
        '#title' => t('Use Permission'),
        '#options' => array(
          'publisher' => t('Contact Publisher (I do not hold copyright).'),
          'author' => t('Contact Author (I hold the copyright and wish to retain all rights).'),
        ),
        '#required' => TRUE,
      ),
      'certifying' => array(
        '#type' => 'checkboxes',
        '#title' => t('Certify'),
        '#options' => array(
         'certify' => t('I certify that I have the right to upload this version.'),
        ),
        '#required' => TRUE,
      ),
      'submit' => array(
        '#type' => 'submit',
        '#value' => t('Upload'),
      ),
    ),
    '#action' => url("fedora/repository/$pid", array('fragment' => 'attach-doc')),
    '#attributes' => array('enctype' => "multipart/form-data"),
  );
}

//XXX:  Not the best check, but should be alright...
function _islandora_scholar_check_mimetype($file, $desired_mimetype) {
  if ($file->filemime == $desired_mimetype) {
    return array();
  }
  elseif ($file->filemime == 'application/octet-stream') {
    module_load_include('inc', 'fedora_repository', 'MimeClass');
    $mc = new MimeClass();
    if ($mc->get_mimemtype($file->filename) == $desired_mimetype) {
      return array();
    }
  }

  return array(t('The file %filename does not have the required mimetype %mime.', array(
    '%filename' => $file->filename,
    '%mime' => $desired_mimetype,
  )));
}

function islandora_scholar_upload_form_validate($form, &$form_state) {
  $form_state['values']['file'] = file_save_upload('file', array(
    '_islandora_scholar_check_mimetype' => array('application/pdf'),
  ));
  if ($form_state['values']['file'] === 0) {
    form_set_error('file', t('No file uploaded.'));
  }
}

function _islandora_scholar_add_usage_and_version_elements_to_mods($item, $usage, $version) {
  if (array_key_exists('MODS', $item->datastreams)) {
    $mods_str = trim($item->get_datastream_dissemination('MODS'));
    if (!$mods_str) {
      drupal_set_message(t('MODS in object @pid is empty...  Refusing to add ' .
        'usage and publication version statements alone.', array(
          '@pid' => $item->pid
        )
      ), 'error');
      return FALSE;
    }

    //Load the MODS...
    $mods_doc = DOMDocument::loadXML($mods_str);
    $mods_xpath = new DOMXPath($mods_doc);
    $mods_ns = 'http://www.loc.gov/mods/v3';
    $mods_xpath->registerNamespace('m', $mods_ns);
    $mods_doc =& $mods_xpath->document;  //DOMXPath creates a copy...  let's use it instead.

    $mods_element = NULL;
    $mods_elements = $mods_xpath->query('//m:mods');
    if (count($mods_elements) > 0) {
      $mods_element = $mods_elements->item(0);
    }
    else {
      drupal_set_message(t('Could not find MODS element in object @pid is ' .
        'empty...  Refusing to add usage and publication version statements' .
        ' alone.', array(
          '@pid' => $item->pid
        )
      ), 'error');
      return FALSE;
    }

    $usage_element = NULL;
    $usage_elements = $mods_xpath->query('m:accessCondition[@type="use and reproduction"]', $mods_element);
    if ($usage_elements->length > 0) {
      //Found at least one...  Grab the first?
      $usage_element = $usage_elements->item(0);
    }
    else {
      //Didn't find any...  Create element with attributes.
      $usage_element = $mods_doc->createElementNS('http://www.loc.gov/mods/v3', 'accessCondition');
      $usage_element->setAttribute('type', 'use and reproduction');
      $mods_element->appendChild($usage_element);
    }

    //Delete all children
    while($node = $usage_element->firstChild) {
      $usage_element->removeChild($node);
    }
    $usage_element->appendChild($mods_doc->createTextNode($usage));

    $version_element = NULL;
    $version_elements = $mods_xpath->query('m:physicalDescription[@authority="local"]', $mods_element);
    if ($version_elements->length > 0) {
      //Found at least one...  Grab the first?
      $version_element = $version_elements->item(0);
    }
    else {
      //Didn't find any...  Create element with attributes.
      $version_element = $mods_doc->createElementNS('http://www.loc.gov/mods/v3', 'physicalDescription');
      $version_element->setAttribute('authority', 'local');
      $mods_element->appendChild($version_element);
    }

    //Delete all children
    while($node = $version_element->firstChild) {
      $version_element->removeChild($node);
    }
    $version_element->appendChild($mods_doc->createTextNode($version));

    $item->modify_datastream($mods_doc->saveXML(), 'MODS', 'MODS datastream', 'text/xml');
  }
  else {
    drupal_set_message(t('No MODS in object @pid...  Refusing to add usage and ' .
      'publication version statements alone.', array(
        '@pid' => $item->pid
      )
    ), 'error');
  }
}

function islandora_scholar_upload_form_submit(&$form, &$form_state) {
  extract($form_state['values']);

  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  $item = new Fedora_Item($pid);

  $status = $item->add_datastream_from_file($file->filepath, $dsid, 'PDF datastream', 'application/pdf');

  //Add datastream should return the DSID added on success...
  if ($status !== NULL && $status->datastreamID == $dsid) {
    $form_state['redirect'] = array("fedora/repository/$pid", NULL, 'overview');

    _islandora_scholar_add_usage_and_version_elements_to_mods($item, $usage, $version);
  }
  else {
    drupal_set_message(t('DON\'T PANIC!, but we may have failed adding the ' .
      'datastream...  Some debug info: @info', array('@info' => print_r($status, TRUE))));
  }
}
