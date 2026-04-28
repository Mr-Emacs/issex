;;; issex.el --- Emacs integration for the issex issue tracker -*- lexical-binding: t -*-

;; Usage: (load "/path/to/issex.el") or add to init.el
;; Keybindings are under the prefix C-c i
;;
;; C-c i l   - list all tasks
;; C-c i L   - list with a query (e.g. "s=1 & p>2")
;; C-c i a   - add a task (prompts for name, priority, notes, tags)
;; C-c i c   - close task at point (or prompt for HUID)
;; C-c i g   - go to task (jumps via TASK: HUID on current line, or list selection)
;; C-c i s o - list only OPEN tasks
;; C-c i s c - list only CLOSED tasks
;; C-c i s p - list sorted by priority (high first)
;; C-c i s t - list by tag (prompts for tag name)
;; C-c i i   - insert task comment at point (inserts TASK: HUID + first note line)

; TASK: Abobaalskdjflajsd_20260414_194820 - Abobaalskdjflajsd


(defgroup issex nil
  "Emacs integration for the issex issue tracker."
  :group 'tools
  :prefix "issex-")

(defcustom issex-executable "issex"
  "Path to the issex executable."
  :type 'string
  :group 'issex)

(defcustom issex-comment-format "TASK: %s - %s"
  "Format for inserted task comments. First %s is HUID, second is note."
  :type 'string
  :group 'issex)

(defvar issex--list-buffer "*issex-tasks*")
(defvar issex--last-query nil)

(defconst issex--line-re
  "\\([^\n]+[/\\\\]notes\\.md\\):\\([0-9]+\\): \\[\\(OPEN\\|CLOSED\\)\\] P:\\([0-9]+\\) - \\(.*?\\) (HUID: \\([^)]+\\))")

(defun issex--run (&rest args)
  (let* ((cmd (mapconcat #'identity (cons issex-executable args) " "))
         (buf (generate-new-buffer " *issex-run*"))
         (code (call-process-shell-command cmd nil buf nil))
         (out  (with-current-buffer buf (buffer-string))))
    (kill-buffer buf)
    (cons code out)))

(defun issex--run-list (&optional query)
  (let ((result (if (and query (not (string-empty-p query)))
                    (issex--run "list" "-src" query)
                  (issex--run "list"))))
    (if (= (car result) 0)
        (cdr result)
      (error "issex error: %s" (cdr result)))))

(defun issex--parse-lines (raw)
  (let (tasks)
    (dolist (line (split-string raw "\n" t))
      (when (string-match issex--line-re line)
        (push (list :notes    (match-string 1 line)
                    :line     (string-to-number (match-string 2 line))
                    :status   (match-string 3 line)
                    :priority (string-to-number (match-string 4 line))
                    :title    (match-string 5 line)
                    :huid     (match-string 6 line))
              tasks)))
    (nreverse tasks)))

(defun issex--get-tags (notes-path)
  (when (and notes-path (file-exists-p notes-path))
    (with-temp-buffer
      (insert-file-contents notes-path)
      (goto-char (point-min))
      (when (re-search-forward "^## TAGS: *\\(.*\\)$" nil t)
        (let ((raw (string-trim (match-string 1))))
          (unless (string-empty-p raw)
            (mapcar #'string-trim (split-string raw ","))))))))

(defun issex--find-notes-for-huid (huid)
  (let* ((raw   (issex--run-list))
         (tasks (issex--parse-lines raw))
         (match (cl-find huid tasks
                         :key  (lambda (tk) (plist-get tk :huid))
                         :test #'string=)))
    (when match (plist-get match :notes))))

(defface issex-open-face     '((t :foreground "green"  :weight bold))  "")
(defface issex-closed-face   '((t :foreground "gray"   :slant italic)) "")
(defface issex-priority-face '((t :foreground "orange"))               "")
(defface issex-huid-face     '((t :foreground "cyan"))                 "")

(defvar issex-list-mode-map
  (let ((m (make-sparse-keymap)))
    (set-keymap-parent m tabulated-list-mode-map)
    ;; keybinds
    (define-key m (kbd "RET") #'issex-goto-task-at-point)
    (define-key m (kbd "g")   #'issex-refresh)
    (define-key m (kbd "c")   #'issex-close-at-point)
    (define-key m (kbd "so")  #'issex-filter-open)
    (define-key m (kbd "sc")  #'issex-filter-closed)
    (define-key m (kbd "sp")  #'issex-sort-by-priority)
    (define-key m (kbd "st")  #'issex-filter-by-tag)
    (define-key m (kbd "q")   #'quit-window)
    (define-key m (kbd "?")   #'issex-list-help)
    m))

(define-derived-mode issex-list-mode tabulated-list-mode "Issex"
  (setq tabulated-list-format
        [("Status" 7  t)
         ("P"      4  (lambda (a b)
                        (< (string-to-number (aref (cadr a) 1))
                           (string-to-number (aref (cadr b) 1)))))
         ("Title"  40 t)
         ("Tags"   20 t)
         ("HUID"   0  t)])
  (setq tabulated-list-padding 1)
  (tabulated-list-init-header)
  (hl-line-mode 1))

(defun issex--task-to-entry (task)
  (let* ((status   (plist-get task :status))
         (priority (plist-get task :priority))
         (title    (plist-get task :title))
         (huid     (plist-get task :huid))
         (notes    (plist-get task :notes))
         (open     (string= status "OPEN"))
         (tags     (mapconcat #'identity
                              (or (issex--get-tags notes) '("-"))
                              ", ")))
    (list huid
          (vector
           (propertize status 'face (if open 'issex-open-face 'issex-closed-face))
           (propertize (number-to-string priority) 'face 'issex-priority-face)
           title
           tags
           (propertize huid 'face 'issex-huid-face)))))

(defun issex--populate-buffer (buf tasks)
  (with-current-buffer buf
    (let ((inhibit-read-only t))
      (issex-list-mode)
      (setq tabulated-list-entries (mapcar #'issex--task-to-entry tasks))
      (tabulated-list-print t)
      (goto-char (point-min)))))

(defun issex--open-list-buffer (tasks &optional query)
  (setq issex--last-query query)
  (let* ((buf (get-buffer-create issex--list-buffer))
         (win (get-buffer-window buf)))
    (issex--populate-buffer buf tasks)
    (if win
        (with-selected-window win
          (goto-char (point-min)))
      (pop-to-buffer buf))))

(defun issex--huid-at-point ()
  (if (derived-mode-p 'issex-list-mode)
      (tabulated-list-get-id)
    (let ((line (buffer-substring-no-properties
                 (line-beginning-position) (line-end-position))))
      (when (string-match "TASK: \\([^ \t\n]+\\)" line)
        (match-string 1 line)))))

;;;###autoload
(defun issex-goto-task-at-point ()
  (interactive)
  (let ((huid (or (issex--huid-at-point) (read-string "HUID: "))))
    (issex--jump-to-task huid)))

(defun issex--jump-to-task (huid)
  (let ((notes (issex--find-notes-for-huid huid)))
    (if (not notes)
        (error "Could not find task: %s" huid)
      (let ((buf (find-file-noselect notes)))
        (switch-to-buffer-other-window buf)
        (with-current-buffer buf
          (goto-char (point-min))
          (re-search-forward "^## NOTES:" nil t)
          (forward-line 1))))))

;;;###autoload
(defun issex-list ()
  (interactive)
  (issex--open-list-buffer (issex--parse-lines (issex--run-list))))

;;;###autoload
(defun issex-list-query (query)
  (interactive "sQuery: ")
  (issex--open-list-buffer (issex--parse-lines (issex--run-list query)) query))

;;;###autoload
(defun issex-filter-open ()
  (interactive)
  (issex--open-list-buffer (issex--parse-lines (issex--run-list "s=1")) "s=1"))

;;;###autoload
(defun issex-filter-closed ()
  (interactive)
  (issex--open-list-buffer (issex--parse-lines (issex--run-list "s=0")) "s=0"))

;;;###autoload
(defun issex-sort-by-priority ()
  (interactive)
  (issex--open-list-buffer
   (sort (issex--parse-lines (issex--run-list))
         (lambda (a b) (> (plist-get a :priority) (plist-get b :priority))))
   "sorted by priority"))

;;;###autoload
(defun issex-filter-by-tag (tag)
  (interactive "sTag: ")
  (let ((query (format "t=%s" tag)))
    (issex--open-list-buffer (issex--parse-lines (issex--run-list query)) query)))

;;;###autoload
(defun issex-refresh ()
  (interactive)
  (if issex--last-query
      (issex-list-query issex--last-query)
    (issex-list)))

;;;###autoload
(defun issex-add (name priority notes tags)
  (interactive
   (list (read-string "Task name: ")
         (read-number "Priority: " 0)
         (read-string "Notes: ")
         (read-string "Tags (comma-separated, optional): ")))
  (let* ((args (list "add" "-name" name "-priority" (number-to-string priority)))
         (args (if (and notes (not (string-empty-p notes)))
                   (append args (list "-notes" notes)) args))
         (args (if (and tags (not (string-empty-p tags)))
                   (append args (list "-tags" tags)) args))
         (result (apply #'issex--run args)))
    (if (/= (car result) 0)
        (error "issex add failed: %s" (cdr result))
      (let ((huid (when (string-match "Created issue with ID \\([^ \n]+\\)" (cdr result))
                    (match-string 1 (cdr result)))))
        (message "Created: %s" (or huid "ok"))
        (when huid
          (issex--insert-task-comment huid name notes))))))

(defun issex--insert-task-comment (huid title notes)
  (let* ((text (format issex-comment-format huid
                       (if (and notes (not (string-empty-p notes))) notes title)))
         (cs   (string-trim-right (or comment-start "//"))))
    (beginning-of-line)
    (insert cs " " text "\n")
    (forward-line -1)))

;;;###autoload
(defun issex-close-at-point ()
  (interactive)
  (let* ((huid   (or (issex--huid-at-point) (read-string "HUID to close: ")))
         (result (issex--run "close" "-id" huid)))
    (if (= (car result) 0)
        (progn (message "Closed %s" huid) (issex-refresh))
      (error "issex close failed: %s" (cdr result)))))

;;;###autoload
(defun issex-insert-task-comment ()
  (interactive)
  (let* ((tasks   (issex--parse-lines (issex--run-list)))
         (choices (mapcar (lambda (tk)
                            (cons (format "[%s] P:%d  %s  %s"
                                          (plist-get tk :status)
                                          (plist-get tk :priority)
                                          (plist-get tk :title)
                                          (plist-get tk :huid))
                                  tk))
                          tasks))
         (choice  (completing-read "Task: " (mapcar #'car choices)))
         (task    (cdr (assoc choice choices))))
    (issex--insert-task-comment
     (plist-get task :huid)
     (plist-get task :title)
     nil)))

(defun issex-list-help ()
  (interactive)
  (message "RET:open  g:refresh  c:close  so:open  sc:closed  sp:priority  st:tag  q:quit"))

(defvar issex-map
  (let ((m (make-sparse-keymap)))
    ;; keybinds
    (define-key m (kbd "l")  #'issex-list)
    (define-key m (kbd "L")  #'issex-list-query)
    (define-key m (kbd "a")  #'issex-add)
    (define-key m (kbd "c")  #'issex-close-at-point)
    (define-key m (kbd "g")  #'issex-goto-task-at-point)
    (define-key m (kbd "i")  #'issex-insert-task-comment)
    (define-key m (kbd "so") #'issex-filter-open)
    (define-key m (kbd "sc") #'issex-filter-closed)
    (define-key m (kbd "sp") #'issex-sort-by-priority)
    (define-key m (kbd "st") #'issex-filter-by-tag)
    m))

(add-to-list 'display-buffer-alist
             '("\\*issex-tasks\\*"
               (display-buffer-reuse-window display-buffer-use-some-window)
               (inhibit-same-window . nil)))

(global-set-key (kbd "C-c i") issex-map)

(provide 'issex)
;;; issex.el ends here
